/* Real numeric-map storage, trigger ownership and controller evaluation. The
 * host boundary supplies only a checked single-threaded SDL lock. */
static int run_map_leaf(Player *p, StateCtrl *sc, ExprEvalCtx *frame, void *context)
{
    assert(context==&owner && sc->type==SCTRL_ZSS_MAP);
    return ZssMap_Apply(p,sc,frame);
}
static void map_expect(Player *p, const char *key, int type, double expected)
{
    ExprValue value={0};
    assert(ZssMap_Read(p,key,&value)==type);
    if (type==EXPR_INT) assert(value.i==(int)expected);
    if (type==EXPR_FLOAT) assert(value.f==(float)expected);
}
static Ini *map_defaults(const ScriptParam *pairs, size_t count)
{
    const char *error;
    Ini *ini=ScriptParams_ToIni(pairs,count,&error);
    assert(ini && !error && strlen(ini->lines[0])==5);
    memcpy(ini->lines[0],"[map]",5);
    return ini;
}
static void test_zss_ai_map(void)
{
    const char *bad[]={
        "MapSet{}","MapAdd{map:foo}","MapSet{map:\"\";value:1}",
        "MapSet{map:$x;value:1}","MapSet{map:\"x\"+$x;value:1}",
        "MapSet{map:foo(1);value:1}","MapSet{map:\"a[1]\";value:1}",
        "MapSet{map:foo;value:1;redirectid:1}","ParentMapSet{map:foo;value:1}",
        "RootMapAdd{map:foo;value:1}","TeamMapSet{map:foo;value:1}",
        "MapReset{exclude:\"x\"}","MapReset{value:1}",
        "MapSet{map:foo;value:\"text\"}","MapSet{map:foo;value:$unknown}",
        "MapAdd{map:foo;value:1,2}","MapSet{map:foo;value:2;MAP:bar}"
    };
    const char *bad_expr[]={"map()","map(\"\")","map($x)","map(foo,1)",
        "map(\"a\"+$x)","map(foo):=1","map(foo[1])","map(\"a\\nb\")","aiLevelF(1)"};
    const char *source;
    const char *why, *end;
    ScriptParam pairs[]={{"Power","44",1},{"Scale","1.5",2}};
    ScriptParam wrong[]={{"bad","var(1)",1}};
    Ini *ini;
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprEvalCtx frame={0};
    Expr expression={0}, copy={0};
    ExprValue value={0};
    StateCtrl sc={0};
    ZssDiagnostic diagnostic;
    ZssProgram *program=NULL;
    static Player other, slots[ZSS_MAP_PLAYERS_MAX+1];
    char text[256], key[ZSS_MAP_KEY_MAX+1], long_key[ZSS_MAP_KEY_MAX+2];
    int i, integer, calls, before, result;
    float real;
    size_t j;

    player.id=1001;player.data=&owner;other.id=1002;other.data=&owner;
    ZssMap_ResetPlayer(&player);ZssMap_ResetPlayer(&other);
    assert(ZssMap_ParseKey("  \"MiX ed\"  )",key,&end) && !strcmp(key,"mix ed") && *end==')');
    map_expect(&player,"missing",EXPR_INT,0);
    before=zss_calloc_calls;map_expect(&player,"missing",EXPR_INT,0);assert(zss_calloc_calls==before);

    /* Float result from the actor's ramped level, including human/training and
     * the reference MUGEN-1.x helper exception. No global-difficulty shortcut. */
    player.ai=1;player.ai_level=7;player.helper=0;game.team_variant=0;
    assert(Expr_CompileCtx("aiLevelF/2",&expression,&owner,NULL,EXPR_WANT_ANY,&compile));
    assert(Expr_EvalCtx(&player,&expression,&integer,&real,&frame)==2 && real==3.5f);
    player.ai=0;assert(Expr_EvalCtx(&player,&expression,&integer,&real,&frame)==2 && real==0);
    player.ai=1;game.team_variant=3;player.dummy_control=0;
    assert(Zss_AiLevelF(&player)==0);player.dummy_control=1;assert(Zss_AiLevelF(&player)==7);
    game.team_variant=0;other.helper=1;other.root=&player;
    assert(Zss_AiLevelF(&other)==0);owner.mugenversion=0;assert(Zss_AiLevelF(&other)==7);
    owner.mugenversion=0x01010000;other.helper=0;other.root=NULL;
    Expr_Free(&expression);
    assert(!Expr_Compile("aiLevelF",&expression,&owner,NULL,EXPR_WANT_ANY));Expr_Free(&expression);
    assert(!Expr_Compile("map(foo)",&expression,&owner,NULL,EXPR_WANT_ANY));Expr_Free(&expression);

    for (j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        assert(!Zss_CompileControllerCtx(bad[j],strlen(bad[j]),&owner,&sc,&diagnostic,&compile));
        StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    }
    for (j=0;j<sizeof(bad_expr)/sizeof(*bad_expr);j++) {
        end=NULL;
        result=Expr_CompileCtx(bad_expr[j],&expression,&owner,&end,EXPR_WANT_ANY,&compile);
        assert(!result || (end!=NULL && *end!=0));Expr_Free(&expression);zss_no_allocs();
        snprintf(text,sizeof(text),"MapSet{map:result;value:%s}",bad_expr[j]);
        assert(!Zss_CompileControllerCtx(text,strlen(text),&owner,&sc,&diagnostic,&compile));
        StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    }
    memset(long_key,'a',sizeof(long_key)-1);long_key[sizeof(long_key)-1]=0;
    snprintf(text,sizeof(text),"MapSet{map:%s;value:1}",long_key);
    assert(!Zss_CompileController(text,strlen(text),&owner,&sc,&diagnostic));
    StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    long_key[ZSS_MAP_KEY_MAX]=0;
    assert(ZssMap_ParseKey(long_key,key,&end) && *end==0 && strlen(key)==ZSS_MAP_KEY_MAX);
    value.i=23;assert(ZssMap_Write(&player,long_key,value,EXPR_INT,0));
    map_expect(&player,key,EXPR_INT,23);ZssMap_ResetPlayer(&player);zss_no_allocs();

    source="MapSet{map:\"Mixed\";value:cond($x>0,map(existing)+$x,var(2):=1)}";
    zss_calloc_calls=0;
    assert(Zss_CompileControllerCtx(source,strlen(source),&owner,&sc,&diagnostic,&compile));
    calls=zss_calloc_calls;StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    for (i=1;i<=calls;i++) {
        zss_calloc_calls=0;zss_fail_call=i;
        if (Zss_CompileControllerCtx(source,strlen(source),&owner,&sc,&diagnostic,&compile)) StateCtrl_Free(&sc);
        zss_fail_call=0;memset(&sc,0,sizeof(sc));zss_no_allocs();
    }
    source="[StateDef 0]\nlet x=5;\nlet y=1.5;\n"
        "MapSet{map:\"MiXeD\";value:$x}\nMapAdd{map:mixed;value:$y}\n"
        "if map(MIXED)=6.5 && map(unset)=0 {var(1):=73;}\n"
        "MapSet{map:once;value:var(2):=var(2)+1}\n"
        "MapReset{}\nif map(mixed)=0 && map(once)=0 {var(3):=74;}\n";
    if (!Zss_CompileProgram(source,strlen(source),&owner,&program,&diagnostic)) {
        fprintf(stderr,"map program %zu:%zu: %s\n",diagnostic.line,diagnostic.column,diagnostic.message);abort();
    }
    player.var[1]=player.var[2]=player.var[3]=0;
    assert(Zss_RunBody(program,0,&player,run_map_leaf,&owner,&result));
    assert(player.var[1]==73 && player.var[2]==1 && player.var[3]==74);
    Zss_FreeProgram(program);program=NULL;ZssMap_ResetPlayer(&player);zss_no_allocs();

    /* Shared defaults do not share per-player mutations. Reload is atomic. */
    ini=ScriptParams_ToIni(wrong,1,&why);assert(ini);
    assert(ZssMap_LoadDefaults(ini,&owner,&why));Ini_Free(ini);zss_no_allocs();
    ini=map_defaults(pairs,2);assert(ZssMap_LoadDefaults(ini,&owner,&why));Ini_Free(ini);
    map_expect(&player,"POWER",EXPR_INT,44);map_expect(&other,"scale",EXPR_FLOAT,1.5);
    other.helper=1;map_expect(&other,"power",EXPR_INT,0);
    value.i=11;assert(ZssMap_Write(&other,"power",value,EXPR_INT,0));
    map_expect(&other,"power",EXPR_INT,11);ZssMap_ResetPlayer(&other);
    map_expect(&other,"power",EXPR_INT,0);other.helper=0;
    value.i=3;assert(ZssMap_Write(&player,"power",value,EXPR_INT,1));
    map_expect(&player,"Power",EXPR_INT,47);map_expect(&other,"power",EXPR_INT,44);
    value.f=0.25f;assert(ZssMap_Write(&player,"POWER",value,EXPR_FLOAT,1));map_expect(&player,"power",EXPR_FLOAT,47.25);
    value.i=INT_MAX;assert(ZssMap_Write(&player,"wrap",value,EXPR_INT,0));
    value.i=1;assert(ZssMap_Write(&player,"wrap",value,EXPR_INT,1));map_expect(&player,"wrap",EXPR_INT,INT_MIN);
    assert(ZssMap_Write(&player,"bottom",value,EXPR_BOTTOM,0));
    assert(ZssMap_Write(&player,"bottom",value,EXPR_INT,1));map_expect(&player,"bottom",EXPR_BOTTOM,0);
    assert(ZssMap_Write(&player,"new-bottom",value,EXPR_BOTTOM,1));map_expect(&player,"new-bottom",EXPR_BOTTOM,0);

    /* Production trigger payloads survive cloning and use the runtime type. */
    assert(Expr_CompileCtx("map(\"PoWeR\")",&expression,&owner,NULL,EXPR_WANT_ANY,&compile));
    assert(Expr_Clone(&copy,&expression));Expr_Free(&expression);
    assert(Expr_EvalCtx(&player,&copy,&integer,&real,&frame)==2 && real==47.25f);
    assert(Expr_EvalCtx(&other,&copy,&integer,&real,&frame)==1 && integer==44);
    Expr_Free(&copy);
    assert(Expr_CompileCtx("helper(2),map(power)",&expression,&owner,NULL,EXPR_WANT_ANY,&compile));
    assert(Expr_Clone(&copy,&expression));Expr_Free(&expression);Expr_Free(&copy);
    ZssMap_ResetPlayer(&player);map_expect(&player,"wrap",EXPR_INT,0);map_expect(&player,"power",EXPR_INT,44);
    value.i=99;assert(ZssMap_Write(&player,"power",value,EXPR_INT,0));
    player.id++;map_expect(&player,"power",EXPR_INT,44); /* pool reuse */

    ini=map_defaults(wrong,1);assert(!ZssMap_LoadDefaults(ini,&owner,&why) && why);Ini_Free(ini);
    map_expect(&player,"power",EXPR_INT,44);
    ini=map_defaults(pairs,2);zss_calloc_calls=0;
    assert(ZssMap_LoadDefaults(ini,&owner,&why));calls=zss_calloc_calls;
    assert(calls>0);
    for (i=1;i<=calls;i++) {
        zss_calloc_calls=0;zss_fail_call=i;assert(!ZssMap_LoadDefaults(ini,&owner,&why));zss_fail_call=0;
        map_expect(&player,"power",EXPR_INT,44);
    }
    Ini_Free(ini);ZssMap_FreeDefaults(&owner);zss_no_allocs();

    ini=map_defaults(pairs,2);assert(ZssMap_LoadDefaults(ini,&owner,&why));Ini_Free(ini);
    value.i=3;
    for (i=0;i<ZSS_MAP_KEYS_MAX-2;i++) {
        snprintf(key,sizeof(key),"extra%d",i);assert(ZssMap_Write(&player,key,value,EXPR_INT,0));
    }
    assert(!ZssMap_Write(&player,"one-too-many",value,EXPR_INT,0));
    assert(ZssMap_Write(&player,"power",value,EXPR_INT,0));map_expect(&player,"power",EXPR_INT,3);
    ZssMap_ResetPlayer(&player);map_expect(&player,"power",EXPR_INT,44);
    ZssMap_FreeDefaults(&owner);zss_no_allocs();

    /* First-write and growth failures leave existing contents unchanged. */
    value.i=5;zss_calloc_calls=0;zss_fail_call=1;
    assert(!ZssMap_Write(&player,"first",value,EXPR_INT,0));zss_fail_call=0;map_expect(&player,"first",EXPR_INT,0);
    for (i=0;i<8;i++) {snprintf(key,sizeof(key),"k%d",i);assert(ZssMap_Write(&player,key,value,EXPR_INT,0));}
    zss_calloc_calls=0;zss_fail_call=1;assert(!ZssMap_Write(&player,"grow",value,EXPR_INT,0));zss_fail_call=0;
    map_expect(&player,"k7",EXPR_INT,5);map_expect(&player,"grow",EXPR_INT,0);
    for (i=8;i<ZSS_MAP_KEYS_MAX;i++) {snprintf(key,sizeof(key),"k%d",i);assert(ZssMap_Write(&player,key,value,EXPR_INT,0));}
    assert(!ZssMap_Write(&player,"overflow",value,EXPR_INT,0));
    assert(ZssMap_Write(&player,"k0",value,EXPR_INT,1));map_expect(&player,"k0",EXPR_INT,10);
    ZssMap_ResetPlayer(&player);zss_no_allocs();
    for (i=0;i<ZSS_MAP_PLAYERS_MAX;i++) {slots[i].id=i;assert(ZssMap_Write(&slots[i],"x",value,EXPR_INT,0));}
    assert(!ZssMap_Write(&slots[ZSS_MAP_PLAYERS_MAX],"x",value,EXPR_INT,0));
    ZssMap_ResetPlayer(&slots[3]);assert(ZssMap_Write(&slots[ZSS_MAP_PLAYERS_MAX],"x",value,EXPR_INT,0));
    for (i=0;i<=ZSS_MAP_PLAYERS_MAX;i++) ZssMap_ResetPlayer(&slots[i]);zss_no_allocs();
    player.ai=player.helper=player.dummy_control=0;player.data=NULL;g_outOfMemory=0;
    puts("ok: ZSS typed maps, AI level, defaults/reset, actor isolation, budgets and allocation failures");
}
