/* Parse real flag names/metadata and apply through the production flag helper.
 * Expected offsets name individual fields rather than reproducing its bank
 * indexing. The native scenario separately exercises StateCtrl_RunCtx. */
static int run_assert_leaf(Player *p, StateCtrl *sc, ExprEvalCtx *frame, void *context)
{
    int i;
    assert(context==&owner && sc->type==SCTRL_ASSERTSPECIAL && frame!=NULL);
    for (i=0;i<3;i++) assert_flag_ctx(p,sc,i,frame);
    return 0;
}
static void test_zss_assertspecial(void)
{
    static const struct {const char *name;int global;size_t offset;} flags[]={
        {"intro",1,offsetof(GameVars,assert_intro)},
        {"roundnotover",1,offsetof(GameVars,assert_roundnotover)},
        {"noko",1,offsetof(GameVars,assert_noko)},
        {"nokosnd",1,offsetof(GameVars,assert_nokosnd)},
        {"nokoslow",1,offsetof(GameVars,assert_nokoslow)},
        {"nomusic",1,offsetof(GameVars,assert_nomusic)},
        {"globalnoshadow",1,offsetof(GameVars,assert_globalnoshadow)},
        {"timerfreeze",1,offsetof(GameVars,assert_timerfreeze)},
        {"nobardisplay",1,offsetof(GameVars,assert_nobardisplay)},
        {"nobg",1,offsetof(GameVars,assert_nobg)},
        {"nofg",1,offsetof(GameVars,assert_nofg)},
        {"nostandguard",0,offsetof(Player,nostandguard)},
        {"nocrouchguard",0,offsetof(Player,nocrouchguard)},
        {"noairguard",0,offsetof(Player,noairguard)},
        {"noautoturn",0,offsetof(Player,noautoturn)},
        {"noshadow",0,offsetof(Player,noshadow)},
        {"nojugglecheck",0,offsetof(Player,nojugglecheck)},
        {"nowalk",0,offsetof(Player,nowalk)},
        {"unguardable",0,offsetof(Player,unguardable)},
        {"invisible",0,offsetof(Player,invisible)}
    };
    const char *bad[]={
        "AssertSpecial{}","AssertSpecial{flag2:nowalk}","AssertSpecial{flag3:invisible}",
        "AssertSpecial{flag:NoSuchFlag}","AssertSpecial{flag:nowalk;flag2:noailevel}",
        "AssertSpecial{flag:nowalk;flag2:intro;flag3:animfreeze}",
        "AssertSpecial{flag:nowalk;enabled:0}","AssertSpecial{flag:nowalk;redirectid:1}",
        "AssertSpecial{flag:nowalk;flag4:invisible}","AssertSpecial{flag:nowalk;flag8:intro}",
        "AssertSpecial{flag:nowalk;value:1}","AssertSpecial{flag:0}",
        "AssertSpecial{flag:\"nowalk\"}","AssertSpecial{flag:$x}",
        "AssertSpecial{flag:cond($x,nowalk,invisible)}","AssertSpecial{flag:nowalk+invisible}",
        "AssertSpecial{flag:nowalk,invisible}","AssertSpecial{flag:nowalk;FLAG:invisible}",
        "AssertSpecial{flag:nowalk;flag2:}","AssertSpecial{flag:nowalk;flag3:$unknown}"
    };
    static GameVars saved_game, expected_game, baseline_game;
    static Player saved_player, expected_player, baseline_player, other, saved_other;
    StateCtrl sc={0}, legacy={0};
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprEvalCtx frame={0};
    ScriptParam pairs[]={{"type","AssertSpecial",1},{"flag","NoWaLk",2},{"flag3","TIMERFREEZE",3}};
    Ini *ini;
    ZssDiagnostic diagnostic;
    ZssProgram *program=NULL;
    const char *source, *error;
    char text[4300], long_name[4096];
    size_t j;
    int i, kind, index, calls, result;

    saved_game=game;saved_player=player;
    assert(StateCtrl_ScriptType("ASSERTSPECIAL")==SCTRL_ASSERTSPECIAL && script_frame_supported(SCTRL_ASSERTSPECIAL));
    for (j=0;j<sizeof(flags)/sizeof(*flags);j++) {
        snprintf(text,sizeof(text),"AssertSpecial{flag:%s}",flags[j].name);
        assert(Zss_CompileController(text,strlen(text),&owner,&sc,&diagnostic));
        assert(sc.type==SCTRL_ASSERTSPECIAL && sc.params==NULL);
        assert(sc.arg[1].u.value.i==-1 && sc.arg[2].u.value.i==-1);
        memset(&player,0x24,sizeof(player));memset(&game,0x35,sizeof(game));
        memset(&other,0x46,sizeof(other));saved_other=other;
        expected_player=player;expected_game=game;
        if (flags[j].global) ((unsigned char *)&expected_game)[flags[j].offset]=1;
        else ((unsigned char *)&expected_player)[flags[j].offset]=1;
        for (i=0;i<3;i++) assert_flag_ctx(&player,&sc,i,&frame);
        assert(!memcmp(&player,&expected_player,sizeof(player)));
        assert(!memcmp(&game,&expected_game,sizeof(game)) && !memcmp(&other,&saved_other,sizeof(other)));
        /* Reassertion is idempotent, with no allocations or target leakage. */
        for (i=0;i<3;i++) assert_flag_ctx(&player,&sc,i,&frame);
        assert(!memcmp(&player,&expected_player,sizeof(player)) && !memcmp(&game,&expected_game,sizeof(game)));
        StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    }
    game=saved_game;player=saved_player;

    /* Optional flag2 may be absent while flag3 is present. CNS and ZSS share
     * exactly the same six constant metadata slots and destination behavior. */
    ini=ScriptParams_ToIni(pairs,3,&error);assert(ini);
    assert(StateCtrl_ParseParams(ini,&legacy,&owner));Ini_Free(ini);
    source="AssertSpecial{flag:NoWaLk;flag3:TIMERFREEZE}";
    assert(Zss_CompileControllerCtx(source,strlen(source),&owner,&sc,&diagnostic,&compile));
    for (i=0;i<6;i++) {
        assert(sc.arg[i].count==EXPR_CONST && sc.arg[i].t.type==EXPR_INT);
        assert(sc.arg[i].u.value.i==legacy.arg[i].u.value.i);
    }
    memset(&player,0,sizeof(player));memset(&game,0,sizeof(game));
    for (i=0;i<3;i++) assert_flag_ctx(&player,&legacy,i,NULL);
    baseline_game=game;baseline_player=player;
    memset(&player,0,sizeof(player));memset(&game,0,sizeof(game));
    for (i=0;i<3;i++) assert_flag_ctx(&player,&sc,i,&frame);
    assert(!memcmp(&game,&baseline_game,sizeof(game)) && !memcmp(&player,&baseline_player,sizeof(player)));
    assert(player.nowalk==1 && !player.noautoturn && game.assert_timerfreeze==1 && !game.assert_intro);
    StateCtrl_Free(&sc);StateCtrl_Free(&legacy);memset(&sc,0,sizeof(sc));memset(&legacy,0,sizeof(legacy));zss_no_allocs();
    game=saved_game;player=saved_player;

    for (j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        assert(!Zss_CompileControllerCtx(bad[j],strlen(bad[j]),&owner,&sc,&diagnostic,&compile));
        assert(diagnostic.message && !sc.type && !sc.params && !compile.depth);zss_no_allocs();
    }
    memset(long_name,'a',sizeof(long_name)-1);long_name[sizeof(long_name)-1]=0;
    kind=77;index=88;
    assert(!AssertSpecial_ParseFlag(NULL,&kind,&index) && kind==77 && index==88);
    assert(!AssertSpecial_ParseFlag(long_name,&kind,&index) && kind==77 && index==88);
    snprintf(text,sizeof(text),"AssertSpecial{flag:%s}",long_name);
    assert(!Zss_CompileController(text,strlen(text),&owner,&sc,&diagnostic));zss_no_allocs();
    pairs[1].value=long_name;ini=ScriptParams_ToIni(pairs,3,&error);assert(ini);
    assert(!StateCtrl_ParseParams(ini,&legacy,&owner));StateCtrl_Free(&legacy);memset(&legacy,0,sizeof(legacy));Ini_Free(ini);zss_no_allocs();
    long_name[63]=0;assert(!AssertSpecial_ParseFlag(long_name,&kind,&index));
    long_name[63]='a';long_name[64]=0;assert(!AssertSpecial_ParseFlag(long_name,&kind,&index));

    source="AssertSpecial{flag:nowalk;flag2:noautoturn;flag3:timerfreeze}";
    zss_calloc_calls=0;assert(Zss_CompileController(source,strlen(source),&owner,&sc,&diagnostic));
    calls=zss_calloc_calls;StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    for (i=1;i<=calls;i++) {
        zss_calloc_calls=0;zss_fail_call=i;
        if (Zss_CompileController(source,strlen(source),&owner,&sc,&diagnostic)) StateCtrl_Free(&sc);
        zss_fail_call=0;memset(&sc,0,sizeof(sc));zss_no_allocs();
    }
    source="[StateDef 0]\nlet enabled=1;\nif $enabled {call hold($enabled);}\n"
        "[Function hold(enabled)]\nif $enabled {AssertSpecial{flag:nowalk;flag2:noautoturn;flag3:timerfreeze}}\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&diagnostic));
    player.nowalk=player.noautoturn=game.assert_timerfreeze=0;
    assert(Zss_RunBody(program,0,&player,run_assert_leaf,&owner,&result) && !result);
    assert(player.nowalk==1 && player.noautoturn==1 && game.assert_timerfreeze==1);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    source="[StateDef 0]\nAssertSpecial{flag:nowalk}\n[Function bad()]\nAssertSpecial{flag:NoSuchFlag}\n";
    assert(!Zss_CompileProgram(source,strlen(source),&owner,&program,&diagnostic) && program==NULL);zss_no_allocs();
    game=saved_game;player=saved_player;g_outOfMemory=0;
    puts("ok: 20 AssertSpecial flags, exact targets, CNS parity, local call frames, rejection and OOM cleanup");
}
