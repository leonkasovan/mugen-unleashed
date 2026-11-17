/* Regression cases required by the unchanged upstream KFM corpus. */
static void test_zss_kfm(void)
{
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprEvalCtx frame={0};
    ExprValue value={.i=7};char tag=EXPR_INT;
    StateCtrl sc={0};ZssDiagnostic d;ZssProgram *program=NULL;
    const char *source,*oom;
    int i,j,calls,result;
    const struct {const char *text;int type;} payloads[]={
        {"HitDef{attr:S,NA;damage:$x,$x;ground.velocity:$x,$x;pausetime:$x,$x;envshake.time:$x}",SCTRL_HITDEF},
        {"ReversalDef{reversal.attr:;pausetime:$x,$x}",SCTRL_REVERSALDEF},
        {"AfterImage{time:$x;length:$x;trans:add;paladd:$x,$x,$x}",SCTRL_AFTERIMAGE},
        {"EnvShake{time:$x;ampl:$x;freq:$x}",SCTRL_ENVSHAKE},
        {"Explod{anim:F($x);pos:$x,$x,pos z;postype:none;facing:$x}",SCTRL_EXPLOD},
        {"Pause{time:$x;pausebg:$x}",SCTRL_PAUSE},
        {"SuperPause{anim:F($x);pos:$x,$x;poweradd:$x;sound:F$x,$x}",SCTRL_SUPERPAUSE},
        {"HitOverride{attr:;stateno:$x;time:$x}",SCTRL_HITOVERRIDE}
    };
    const char *bad[]={
        "HitDef{attr:S,NA;damage:$x,$x;ground.velocity:$x,unknown}",
        "ReversalDef{pausetime:$x,$x}",
        "ReversalDef{reversal.attr:garbage}",
        "AfterImage{time:$x;paladd:$x,$x,unknown}",
        "EnvShake{time:$x;freq:unknown}",
        "HitOverride{stateno:$x;time:unknown}",
        "Pause{time:$x;pausebg:unknown}",
        "SuperPause{anim:F($x);poweradd:unknown}",
        "SuperPause{time:$x;endcmdbuftime:unknown}",
        "Explod{anim:F($x);pos:$x,$x,1}",
        "Explod{anim:F($x);pos:$x,$x,(var(0):=0)}"
    };
    Player saved=player;
    game.game_coord[0]=320;g_gameCoord[0]=320;owner.localcoord[0]=320;
    frame.values=&value;frame.types=&tag;frame.count=1;
    for(j=0;j<(int)(sizeof(payloads)/sizeof(*payloads));j++) {
        zss_calloc_calls=0;
        if(!Zss_CompileControllerCtx(payloads[j].text,strlen(payloads[j].text),&owner,&sc,&d,&compile)) {fprintf(stderr,"payload %d: %s; %s\n",j,d.message,g_libraryErrorText);assert(0);}
        assert(sc.type==payloads[j].type);calls=zss_calloc_calls;
        if(sc.type==SCTRL_HITDEF) assert(Expr_EvalIntCtx(&player,&((HitDefParams *)sc.params)->damage[0],0,&frame)==7);
        if(sc.type==SCTRL_EXPLOD) assert(Expr_EvalFloatCtx(&player,&((ExplodParams *)sc.params)->pos[0],&frame)==7);
        if(sc.type==SCTRL_ENVSHAKE) assert(Expr_EvalIntCtx(&player,&((EnvShakeParams *)sc.params)->time,0,&frame)==7);
        StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
        /* Original dynamic-default Expr_Clone/MulFloat failures are fatal.
         * Test recoverable parser OOM with constant defaults separately; the
         * preceding successful case still checks dynamic clone ownership. */
        oom=j==0 ? "HitDef{attr:S,NA;damage:7,7;ground.velocity:7,7;pausetime:7,7;envshake.time:$x}" :
            j==1 ? "ReversalDef{reversal.attr:;pausetime:7,7}" : payloads[j].text;
        zss_calloc_calls=0;assert(Zss_CompileControllerCtx(oom,strlen(oom),&owner,&sc,&d,&compile));
        calls=zss_calloc_calls;StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));zss_no_allocs();
        for(i=1;i<=calls;i++) {
            zss_calloc_calls=0;zss_fail_call=i;
            if(Zss_CompileControllerCtx(oom,strlen(oom),&owner,&sc,&d,&compile)) StateCtrl_Free(&sc);
            zss_fail_call=0;memset(&sc,0,sizeof(sc));zss_no_allocs();
        }
    }
    for(j=0;j<(int)(sizeof(bad)/sizeof(*bad));j++) {
        assert(!Zss_CompileControllerCtx(bad[j],strlen(bad[j]),&owner,&sc,&d,&compile));
        assert(d.message && !sc.params);zss_no_allocs();
    }
    source="[StateDef 0]\nlet x=5;\nsysVar(1):=($x=[1,8] && $x!=[6,7]);\nsysFVar(2):=1.5;\nvar(0):=sysVar(1):=9;\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(player.var[0]==9 && player.sysvar[1]==9);
    {float f;memcpy(&f,&player.sysfvar[2],sizeof(f));assert(f==1.5f);}
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    source="[StateDef 0]\npersistent(0) if var(0)>0 {var(1):=var(1)+1;}\npersistent(3) {var(2):=var(2)+1;}\nif 0 {var(3):=99;} else {ignoreHitPause {var(3):=var(3)+1;}}\n";
    memset(&player,0,sizeof(player));
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    for(i=0;i<7;i++) {if(i==2)player.var[0]=1;assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));}
    assert(player.var[1]==1 && player.var[2]==3 && player.var[3]==7);
    player.hitpause_time=2;assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(player.var[1]==1 && player.var[2]==3 && player.var[3]==8);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    {
        Trigger trigger={0};int raw,type;
        player.consts.airjuggle=15;juggle_victim.id=42;juggle_entry.player=NULL;
        trigger.id=TRIGGER_ZSS_JUGGLEPOINTS;g_exprStackTop=-1;
        ExprStack_Push(42,EXPR_INT);assert(Trigger_EvalZssNumeric(&player,&trigger));
        assert(ExprStack_Pop(&raw,&type) && raw==15 && type==EXPR_INT);
        juggle_entry.player=&juggle_victim;juggle_entry.juggle=4;
        ExprStack_Push(42,EXPR_INT);assert(Trigger_EvalZssNumeric(&player,&trigger));
        assert(ExprStack_Pop(&raw,&type) && raw==4 && type==EXPR_INT);
        ExprStack_Push(99,EXPR_INT);assert(Trigger_EvalZssNumeric(&player,&trigger));
        assert(ExprStack_Pop(&raw,&type) && raw==15 && type==EXPR_INT);
        assert(g_exprStackTop==-1);juggle_entry.player=NULL;
    }
    player=saved;g_outOfMemory=0;
    puts("ok: KFM payload contexts, recoverable allocation failures, strict rollback, system assignments, intervals and block attributes");
}
