/* Real PlaySnd parsing/evaluation/pan math; slot/mixer APIs are boundaries.
 * Native integration separately exercises SDL_mixer with a dummy audio device. */
static struct {
    Snd *bank;
    int id, group, number, pan, loop, busy, slot, prepared, played, used;
    double volume;
    char order[16];
} sound_capture;
static Snd personal_bank, common_bank;
static void sound_event(char event)
{
    assert(sound_capture.used<15);
    sound_capture.order[sound_capture.used++]=event;
}
int Sound_IsPlaying(int id)
{
    sound_capture.id=id;sound_event('Q');return sound_capture.busy;
}
int Sound_Prepare(int id, Snd *bank, int group, int number)
{
    sound_capture.id=id;sound_capture.bank=bank;
    sound_capture.group=group;sound_capture.number=number;sound_capture.prepared++;
    sound_event('R');return sound_capture.slot;
}
void Sound_SetSlotVolume(int slot, double volume)
{
    assert(slot==7);sound_capture.volume=volume;sound_event('V');
}
void Sound_SetSlotPan(int slot, int pan)
{
    assert(slot==7);sound_capture.pan=pan;sound_event('P');
}
void Sound_SetSlotLoop(int slot, int loop)
{
    assert(slot==7);sound_capture.loop=loop;sound_event('L');
}
void Sound_PlaySlot(int slot)
{
    assert(slot==7);sound_capture.played++;sound_event('S');
}
static void sound_reset(void)
{
    memset(&sound_capture,0,sizeof(sound_capture));sound_capture.slot=7;
    game.sound_enabled=1;game.no_sounds=0;game.player_slots=64;
    game.master_volume=50;game.wav_volume=80;game.stereo_effects=1;
    game.game_coord[0]=320;game.panning_width=100;game.camera_offset_x=25;
    game.fight.common_snd=&common_bank;
    player.data=&owner;player.index=3;player.facing=1;player.pos.x=100;
    owner.snd=&personal_bank;owner.volumescale=50;
}
static int run_sound_leaf(Player *p, StateCtrl *sc, ExprEvalCtx *frame, void *context)
{
    assert(context==&owner && sc->type==SCTRL_PLAYSND);
    PlaySnd_RunCtx(sc->params,p,sc,frame);return 0;
}
static void test_zss_playsnd(void)
{
    const char *source="PlaySnd{value:F $x,$y;channel:$x;volume:0;volumescale:$y*40;freqmul:2;loop:1;abspan:120}";
    const char *effects="PlaySnd{channel:var(32):=var(32)*10+1;value:(var(32):=var(32)*10+2),(var(32):=var(32)*10+3);volume:var(32):=var(32)*10+4;volumescale:var(32):=var(32)*10+5;lowpriority:var(32):=var(32)*10+6;freqmul:var(32):=var(32)*10+7;loop:var(32):=var(32)*10+8;pan:var(32):=var(32)*10+9}";
    const char *bad[]={
        "PlaySnd{}", "PlaySnd{value:}", "PlaySnd{value:F}", "PlaySnd{value:1,}",
        "PlaySnd{value:1,2,3}", "PlaySnd{value:1,$unknown}",
        "PlaySnd{value:$unknown,0}", "PlaySnd{value:F $x,0;channel:$unknown}",
        "PlaySnd{value:1,0;channel:$x,2}", "PlaySnd{value:1,0;freqmul:$unknown}",
        "PlaySnd{value:1,0;pan:$unknown}", "PlaySnd{value:1,0;abspan:$unknown}",
        "PlaySnd{value:1,0;redirectid:1}", "PlaySnd{value:1,0;priority:1}",
        "PlaySnd{value:1,0;loopstart:1}", "PlaySnd{value:1,0;value:2,0}",
        "PlaySnd{value:M 1,0}", "PlaySnd{value:\"F\",1,0}",
        "PlaySnd{value:1,0;loop:$x +}", "PlaySnd{value:1,0;volumescale:$unknown}"
    };
    const char *valid[]={"PlaySnd{value:F40,2}","PlaySnd{value:s 40,2}",
        "PlaySnd{value:40}","PlaySnd{value:floor(1.5),0}",
        "PlaySnd{value:sin(0),0}","PlaySnd{value:F floor(1.5),0}",
        "PlaySnd{value:fvar(0),0}","PlaySnd{value:sysvar(0),0}"};
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprValue values[2]={{0},{0}};
    char types[2]={EXPR_INT,EXPR_FLOAT};
    ExprEvalCtx frame={values,types,2};
    StateCtrl sc={0}, failed={0}, legacy={0};
    PlaySndParams *ps;
    ZssDiagnostic d;
    ZssProgram *program=NULL;
    size_t j;
    int i, calls, result, warnings;
    Ini *ini;
    const char *error;
    sound_reset();values[0].i=3;values[1].f=1.5f;
    assert(StateCtrl_ScriptType("PlaySnd")==SCTRL_PLAYSND && script_frame_supported(SCTRL_PLAYSND));
    assert(Zss_CompileControllerCtx(source,strlen(source),&owner,&sc,&d,&compile));
    ps=sc.params;assert(ps && ps->fight==1 && ps->abspan==1);
    PlaySnd_RunCtx(ps,&player,&sc,&frame);
    assert(sound_capture.bank==&common_bank && sound_capture.id==1195);
    assert(sound_capture.group==3 && sound_capture.number==1 && sound_capture.volume==24);
    assert(sound_capture.pan==127 && sound_capture.loop==1 && sound_capture.played==1);
    assert(!strcmp(sound_capture.order,"RVPLS"));
    sound_reset();values[0].i=5;values[1].f=2.5f;
    PlaySnd_RunCtx(ps,&player,&sc,&frame);
    assert(sound_capture.id==1323 && sound_capture.group==5 && sound_capture.number==2);
    assert(sound_capture.volume==40);
    sound_reset();game.stereo_effects=0;PlaySnd_RunCtx(ps,&player,&sc,&frame);
    assert(sound_capture.pan==0 && sound_capture.played==1);
    StateCtrl_Free(&sc);zss_no_allocs();
    for(j=0;j<sizeof(valid)/sizeof(*valid);j++) {
        assert(Zss_CompileController(valid[j],strlen(valid[j]),&owner,&sc,&d));
        assert(((PlaySndParams *)sc.params)->fight==(j==0 || j==5));
        StateCtrl_Free(&sc);zss_no_allocs();
    }
    for(j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        assert(!Zss_CompileControllerCtx(bad[j],strlen(bad[j]),&owner,&failed,&d,&compile));
        assert(!failed.params && !failed.arg[0].count && d.message && !compile.depth);zss_no_allocs();
    }
    assert(!Zss_CompileController("PlaySnd{value:$x,0}",strlen("PlaySnd{value:$x,0}"),&owner,&failed,&d));
    zss_no_allocs();
    /* Abspan overrides pan, including the original facing multiplication.
     * Volume is evaluated but unused; freqmul is checked, not applied. */
    source="PlaySnd{value:S 40,2;pan:1/0;abspan:120;volume:0;freqmul:2}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));
    sound_reset();player.facing=-1;warnings=player_warnings;
    PlaySnd_Run(sc.params,&player,&sc);
    assert(sound_capture.bank==&personal_bank && sound_capture.volume==20);
    assert(sound_capture.id==-1 && sound_capture.pan==-127 && !sound_capture.loop);
    assert(player_warnings==warnings);StateCtrl_Free(&sc);zss_no_allocs();
    source="PlaySnd{value:40,2;pan:120;freqmul:0;channel:32}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));
    sound_reset();player.facing=-1;warnings=player_warnings_at;
    PlaySnd_Run(sc.params,&player,&sc);
    assert(sound_capture.id==-1 && sound_capture.pan==-8 && player_warnings_at==warnings+2);
    StateCtrl_Free(&sc);zss_no_allocs();
    source="PlaySnd{value:-2,-3}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));
    sound_reset();warnings=player_warnings_at;PlaySnd_Run(sc.params,&player,&sc);
    assert(sound_capture.prepared==1 && sound_capture.group==-1 && sound_capture.number==-1);
    assert(!sound_capture.played && player_warnings_at==warnings+2);StateCtrl_Free(&sc);zss_no_allocs();
    source="PlaySnd{value:0,0;pan:-2147483648}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));
    sound_reset();player.facing=-1;PlaySnd_Run(sc.params,&player,&sc);
    assert(sound_capture.played==1);
    game.game_coord[0]=1;
    assert(Sound_Pan(INT_MIN)==0 && Sound_Pan(INT_MAX)==-127);
    StateCtrl_Free(&sc);zss_no_allocs();
    assert(Zss_CompileController(effects,strlen(effects),&owner,&sc,&d));
    for(i=0;i<5;i++) {
        sound_reset();player.var[32]=0;
        if(i==0) game.sound_enabled=0;
        if(i==1) game.no_sounds=1;
        if(i==2) owner.snd=NULL;
        if(i==3) sound_capture.busy=1;
        if(i==4) sound_capture.slot=-1;
        PlaySnd_RunCtx(sc.params,&player,&sc,&frame);
        assert(player.var[32]==123456789 && !sound_capture.played);
        assert(!strcmp(sound_capture.order,i<3 ? "" : i==3 ? "Q" : "QR"));
    }
    StateCtrl_Free(&sc);zss_no_allocs();sound_reset();
    /* The real structured executor supplies a fresh function-local frame. */
    source="[StateDef 0]\ncall sound(8,2,4);\n[Function sound(group,number,channel)]\nPlaySnd{value:F $group,$number;channel:$channel;volumescale:50}\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,run_sound_leaf,&owner,&result) && !result);
    assert(sound_capture.bank==&common_bank && sound_capture.group==8 && sound_capture.number==2);
    assert(sound_capture.id==1259 && sound_capture.volume==20 && sound_capture.played==1);
    Zss_FreeProgram(program);zss_no_allocs();
    source="PlaySnd{value:F cond($x,$x+1,0),var(1)+1;channel:$x;volume:var(2);volumescale:$y*40;lowpriority:var(3);freqmul:$y;loop:$x;pan:var(4)}";
    zss_calloc_calls=0;
    assert(Zss_CompileControllerCtx(source,strlen(source),&owner,&sc,&d,&compile));calls=zss_calloc_calls;
    StateCtrl_Free(&sc);zss_no_allocs();
    for(i=1;i<=calls;i++) {
        zss_calloc_calls=0;zss_fail_call=i;
        assert(!Zss_CompileControllerCtx(source,strlen(source),&owner,&failed,&d,&compile));
        assert(!failed.params && !failed.arg[0].count && !compile.depth);
        zss_fail_call=0;zss_no_allocs();
    }
    /* CNS keeps its original one-value/trailing-comma acceptance. Partial
     * PlaySnd parse failures now release all owned expressions in either path. */
    {
        ScriptParam pairs[]={{"type","playsnd",1},{"value","F40,2",2},
            {"channel","3",3},{"volume","0",4},{"volumescale","75",5},
            {"freqmul","2",6},{"loop","1",7},{"pan","120",8}};
        int id, pan, loop;
        double volume;
        ini=ScriptParams_ToIni(pairs,8,&error);assert(ini);
        memset(&legacy,0,sizeof(legacy));assert(StateCtrl_ParseParams(ini,&legacy,&owner));
        sound_reset();PlaySnd_Run(legacy.params,&player,&legacy);
        id=sound_capture.id;pan=sound_capture.pan;loop=sound_capture.loop;volume=sound_capture.volume;
        assert(sound_capture.bank==&common_bank && sound_capture.group==40 && sound_capture.number==2);
        StateCtrl_Free(&legacy);Ini_Free(ini);zss_no_allocs();
        source="PlaySnd{value:F40,2;channel:3;volume:0;volumescale:75;freqmul:2;loop:1;pan:120}";
        assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));
        sound_reset();PlaySnd_Run(sc.params,&player,&sc);
        assert(sound_capture.bank==&common_bank && sound_capture.group==40 && sound_capture.number==2);
        assert(sound_capture.id==id && sound_capture.pan==pan && sound_capture.loop==loop && sound_capture.volume==volume);
        assert(!strcmp(sound_capture.order,"RVPLS"));StateCtrl_Free(&sc);zss_no_allocs();
    }
    {
        ScriptParam pairs[]={{"type","playsnd",1},{"value","var(0),var(1)",2},
            {"volume","var(2)",3},{"volumescale","var(3)",4},{"channel","1+",5}};
        memset(&legacy,0,sizeof(legacy));
        ini=ScriptParams_ToIni(pairs,5,&error);assert(ini);
        assert(!StateCtrl_ParseParams(ini,&legacy,&owner));StateCtrl_Free(&legacy);Ini_Free(ini);zss_no_allocs();
        memset(&legacy,0,sizeof(legacy));pairs[1].value="40,";
        ini=ScriptParams_ToIni(pairs,2,&error);assert(ini);
        assert(StateCtrl_ParseParams(ini,&legacy,&owner));
        assert(Expr_EvalInt(&player,&((PlaySndParams *)legacy.params)->number,0)==0);
        StateCtrl_Free(&legacy);Ini_Free(ini);zss_no_allocs();
    }
    PlaySndParams_Free(NULL);PlaySnd_RunCtx(NULL,&player,NULL,NULL);
    source="[Function sound(x)]\nPlaySnd{value:F $x,0}\n[Function unsupported()]\nNoSuchController{}\n";
    program=NULL;
    assert(!Zss_CompileProgram(source,strlen(source),&owner,&program,&d) && !program);
    zss_no_allocs();
    assert(g_exprStackTop==-1);
    puts("ok: PlaySnd local parameters, F/S banks, mute/order/priority/pan behavior, legacy quirks and parse/OOM cleanup");
}
