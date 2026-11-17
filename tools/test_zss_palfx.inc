/* Real PalFX payload compilation/application. Rendering is qualified separately. */
static int run_palfx_leaf(Player *p, StateCtrl *sc, ExprEvalCtx *frame, void *context)
{
    assert(context==&owner && sc->type==SCTRL_PALFX);
    assert(PalFx_ApplyCtx(&p->palfx_def,sc->params,p,sc,frame));return 0;
}
static void test_zss_palfx(void)
{
    const char *names[]={"PalFX","BGPalFX","AllPalFX"};
    const char *bad[]={
        "PalFX{time:$unknown}","PalFX{time:$x,2}","PalFX{color:0.5}",
        "PalFX{add:1,2}","PalFX{mul:1,2,3,4}","PalFX{sinadd:1,2}",
        "PalFX{sinadd:1,2,3,}","PalFX{sinadd:1,2,3,4,5}",
        "PalFX{sinadd:1,2,3,$unknown}","PalFX{mul:256,$unknown,256}",
        "PalFX{add:$x,cond($y,1,$unknown),3}","PalFX{invertall:$x+}",
        "PalFX{hue:1}","PalFX{sinmul:1,2,3}","PalFX{sincolor:1,2}",
        "PalFX{invertblend:1}","PalFX{redirectid:1}","BGPalFX{id:1}",
        "BGPalFX{index:0}","AllPalFX{palfx.time:1}","PalFX{time:1;time:2}",
        "PalFX{time:$x;mul:$x,$x,$x;add:$x,$x,$x;sinadd:$x,$x,$x,$x;invertall:$x;color:$unknown}"
    };
    const char *source;
    const char *error;
    char text[1024];
    StateCtrl sc={0}, failed={0}, legacy={0};
    PalFxParams prefixed={0};
    PalFxDef dst, before, bg_before, all_before, baseline;
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprValue values[2]={{0},{0}};
    char types[2]={EXPR_INT,EXPR_FLOAT};
    ExprEvalCtx frame={values,types,2};
    ZssDiagnostic d;
    ZssProgram *program=NULL;
    Ini *ini;
    size_t j;
    int i, calls, result, warnings;
    values[0].i=3;values[1].f=1.5f;
    for(j=0;j<3;j++) {
        snprintf(text,sizeof(text),"%s{time:$x;color:128+$x;add:$x,$y,cond($x=3,7,9);mul:256-$x,256,$x*100;sinadd:$x+1,$x+2,$x+3,$x+9;invertall:$x>3}",names[j]);
        assert(StateCtrl_ScriptType(names[j])==SCTRL_PALFX && script_frame_supported(SCTRL_PALFX));
        assert(Zss_CompileControllerCtx(text,strlen(text),&owner,&sc,&d,&compile));
        assert(((PalFxParams *)sc.params)->kind==(int)j);
        memset(&before,0x11,sizeof(before));memset(&bg_before,0x22,sizeof(bg_before));
        memset(&all_before,0x33,sizeof(all_before));
        dst=before;game.bg_palfx_def=bg_before;game.all_palfx_def=all_before;
        assert(PalFx_ApplyCtx(j==0 ? &dst : NULL,sc.params,&player,&sc,&frame));
        if(j==1) {assert(!memcmp(&dst,&before,sizeof(dst)));dst=game.bg_palfx_def;}
        if(j==2) {assert(!memcmp(&dst,&before,sizeof(dst)));dst=game.all_palfx_def;}
        if(j!=1) assert(!memcmp(&game.bg_palfx_def,&bg_before,sizeof(bg_before)));
        if(j!=2) assert(!memcmp(&game.all_palfx_def,&all_before,sizeof(all_before)));
        assert(dst.active==3 && dst.time==0 && dst.reset==0 && dst.color==131);
        assert(dst.add[0]==3 && dst.add[1]==1 && dst.add[2]==7);
        assert(dst.mul[0]==253 && dst.mul[1]==256 && dst.mul[2]==300);
        assert(dst.sinadd[0]==4 && dst.sinadd[1]==5 && dst.sinadd[2]==6 && dst.period==12 && !dst.invertall);
        values[0].i=4;values[1].f=2.5f;
        assert(PalFx_ApplyCtx(&dst,sc.params,&player,&sc,&frame));
        if(j==1) dst=game.bg_palfx_def;
        if(j==2) dst=game.all_palfx_def;
        assert(dst.active==4 && dst.color==132 && dst.add[0]==4 && dst.add[1]==2 && dst.add[2]==9);
        assert(dst.mul[0]==252 && dst.mul[2]==400 && dst.sinadd[2]==7 && dst.period==13 && dst.invertall==1);
        values[0].i=3;values[1].f=1.5f;
        StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    }
    source="PalFX{}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));
    memset(&dst,0x55,sizeof(dst));assert(PalFx_Apply(&dst,sc.params,&player,&sc));
    assert(dst.active==0 && dst.color==256 && !dst.time && !dst.reset && !dst.invertall && !dst.period);
    for(i=0;i<3;i++) assert(dst.mul[i]==256 && !dst.add[i] && !dst.sinadd[i]);
    StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    source="PalFX{time:-2;color:257;mul:-1,0,1;add:-2147483648,2147483647,0;sinadd:-1,2,-3,-4;invertall:2}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));warnings=player_warnings_at;
    assert(PalFx_Apply(&dst,sc.params,&player,&sc));
    assert(dst.active==0 && dst.color==257 && dst.mul[0]==-1 && dst.add[0]==INT_MIN && dst.add[1]==INT_MAX);
    assert(dst.period==-4 && dst.invertall==2 && player_warnings_at==warnings+3);
    StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    source="PalFX{time:-1;sinadd:1,2,3}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));warnings=player_warnings_at;
    assert(PalFx_Apply(&dst,sc.params,&player,&sc) && dst.active==-1 && dst.period==0 && dst.sinadd[2]==3);
    assert(player_warnings_at==warnings);StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    for(j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        assert(!Zss_CompileControllerCtx(bad[j],strlen(bad[j]),&owner,&failed,&d,&compile));
        assert(!failed.params && !failed.arg[0].count && d.message && !compile.depth);zss_no_allocs();
    }
    source="PalFX{time:$x}";
    assert(!Zss_CompileController(source,strlen(source),&owner,&failed,&d));zss_no_allocs();
    /* Each destination receives its evaluation position. This detects order
     * changes without a large numeric hash or a duplicated evaluator. */
    source="PalFX{invertall:var(40):=var(40)+1;sinadd:(var(40):=var(40)+1),(var(40):=var(40)+1),(var(40):=var(40)+1),(var(40):=var(40)+1);mul:(var(40):=var(40)+1),(var(40):=var(40)+1),(var(40):=var(40)+1);add:(var(40):=var(40)+1),(var(40):=var(40)+1),(var(40):=var(40)+1);color:var(40):=var(40)+1;time:var(40):=var(40)+1}";
    assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));player.var[40]=0;
    assert(PalFx_Apply(&dst,sc.params,&player,&sc));
    assert(dst.active==1 && dst.color==2 && dst.period==12 && dst.invertall==13 && player.var[40]==13);
    for(i=0;i<3;i++) assert(dst.add[i]==3+i && dst.mul[i]==6+i && dst.sinadd[i]==9+i);
    StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    source="[StateDef 0]\ncall EXFX(7);\n[Function EXFX(time)]\nPalFX{time:$time;add:32,16,0;sinadd:64,32,5,3}\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,run_palfx_leaf,&owner,&result) && !result);
    assert(player.palfx_def.active==7 && player.palfx_def.add[0]==32 && player.palfx_def.sinadd[2]==5 && player.palfx_def.period==3);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    source="PalFX{time:$x;color:128+$x;add:var(1),$x,cond($y,$x,0);mul:$x+200,$x+201,$x+202;sinadd:var(2),$y,$x,var(3);invertall:$x && var(4)}";
    zss_calloc_calls=0;
    assert(Zss_CompileControllerCtx(source,strlen(source),&owner,&sc,&d,&compile));calls=zss_calloc_calls;
    StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
    for(i=1;i<=calls;i++) {
        zss_calloc_calls=0;zss_fail_call=i;
        assert(!Zss_CompileControllerCtx(source,strlen(source),&owner,&failed,&d,&compile));
        assert(!failed.params && !failed.arg[0].count && !compile.depth);
        zss_fail_call=0;zss_no_allocs();
    }
    {
        ScriptParam pairs[]={{"type","palfx",1},{"time","17",2},{"color","128",3},
            {"add","32,16,0",4},{"mul","256,192,128",5},{"sinadd","64,32,5,3",6},{"invertall","1",7}};
        ini=ScriptParams_ToIni(pairs,7,&error);assert(ini);
        assert(StateCtrl_ParseParams(ini,&legacy,&owner));assert(PalFx_Apply(&baseline,legacy.params,&player,&legacy));
        StateCtrl_Free(&legacy);memset(&legacy,0,sizeof(legacy));Ini_Free(ini);zss_no_allocs();
        source="PalFX{time:17;color:128;add:32,16,0;mul:256,192,128;sinadd:64,32,5,3;invertall:1}";
        assert(Zss_CompileController(source,strlen(source),&owner,&sc,&d));
        assert(PalFx_Apply(&dst,sc.params,&player,&sc) && !memcmp(&dst,&baseline,sizeof(dst)));
        StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
        pairs[1].value="var(0)";pairs[3].value="var(1),var(2),var(3)";pairs[4].value="var(4),1+";
        ini=ScriptParams_ToIni(pairs,7,&error);assert(ini);
        assert(!StateCtrl_ParseParams(ini,&legacy,&owner));StateCtrl_Free(&legacy);memset(&legacy,0,sizeof(legacy));
        Ini_Free(ini);zss_no_allocs();
        pairs[1].key="sinadd";pairs[1].value="1,2,3,";
        ini=ScriptParams_ToIni(pairs,2,&error);assert(ini);
        assert(StateCtrl_ParseParams(ini,&legacy,&owner) && PalFx_Apply(&dst,legacy.params,&player,&legacy));
        assert(dst.sinadd[2]==3 && dst.period==0);
        StateCtrl_Free(&legacy);memset(&legacy,0,sizeof(legacy));Ini_Free(ini);zss_no_allocs();
    }
    {
        ScriptParam pairs[]={{"palfx.time","$x",1},{"palfx.add","$x,$y,7",2},{"palfx.sinadd","1,2,3,$x+9",3}};
        ini=ScriptParams_ToIni(pairs,3,&error);assert(ini);
        assert(PalFx_ParseParamsCtx(&prefixed,ini,&owner,1,&compile));
        assert(PalFx_ApplyCtx(&dst,&prefixed,&player,NULL,&frame));
        assert(dst.active==3 && dst.add[0]==3 && dst.add[1]==1 && dst.period==12);
        PalFxParams_Free(&prefixed);Ini_Free(ini);zss_no_allocs();
    }
    source="[Function EXFX(t)]\nPalFX{time:$t;add:32,16,0;sinadd:64,32,5,3}\n[Function bad()]\nNoSuchController{}\n";
    assert(!Zss_CompileProgram(source,strlen(source),&owner,&program,&d) && !program);zss_no_allocs();
    PalFxParams_Free(NULL);assert(!PalFx_ApplyCtx(NULL,NULL,NULL,NULL,NULL));
    assert(g_exprStackTop==-1);
    puts("ok: PalFX/BGPalFX/AllPalFX contexts, targets, defaults/order, legacy semantics and parse/OOM cleanup");
}
