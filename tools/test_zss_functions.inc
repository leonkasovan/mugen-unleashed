/* Included by the real expression/controller host harness. */
void Log_Printf(const char *format, ...) { (void)format; }
int Test_ZssBoundLeaf(Player *p, StateCtrl *sc, StateRunCtx *context, ExprEvalCtx *frame)
{
    (void)p;(void)sc;(void)context;(void)frame;abort();
}
static void run_function_program(ZssProgram *program)
{
    int result=-1;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && g_exprStackTop==-1);
}
static void run_function_bound(StateDefs *defs, int number)
{
    int slot=KeyedArray_Find(defs->list,number,0);
    StateDef *sd;
    StateCtrl *sc;
    StateRunCtx context={0};
    assert(slot>=0);
    sd=(StateDef *)(defs->list->data+defs->list->elem_size*slot);
    sc=(StateCtrl *)(sd->ctrls->data+sd->ctrls->elem_size*sd->ctrls->first);
    assert(!Zss_RunBoundBody(&player,sc->params,&context));
    assert(g_exprStackTop==-1);
}
static StateDefs *empty_function_defs(void)
{
    StateDefs *defs=calloc(1,sizeof(*defs));assert(defs);
    defs->list=KeyedArray_Create(1,sizeof(StateDef));assert(defs->list);
    return defs;
}
static void test_zss_functions(void)
{
    const char *source=
        "[StateDef 0]\n"
        "let caller=73;\n"
        "let a,b=call PaIr(1.5, 7);\n"
        "var(0):=$a;\nfvar(0):=$b;\n"
        "let _,c=call pair(2,8);\nvar(1):=$c;\n"
        "call pair(3,9);\n"
        "let sum=call total(5);\nvar(2):=$sum;\n"
        "let old=call fresh(1);\nlet empty=call fresh(0);\n"
        "var(3):=$old;\nif $empty {var(3):=99;}\nvar(4):=$caller;\n"
        "let bottom=call identity(1/0);\nif $bottom {var(3):=99;}\n"
        "let nan=call identity(fvar(31));\nif $nan {var(3):=99;}\n"
        "var(5):=0;\ncall order(var(5):=var(5)+1,var(5):=var(5)+1);\n"
        "call order(1=[1,2],cond(1,4,8));\n"
        "let min=call identity(-2147483648);\nvar(8):=$min;\n"
        "for i=0;2 { call loop(); }\n"
        "call empty();\nvar(10):=123;\n"
        "[Function pair(x,y) a,b]\nlet a=$y;\nlet b=$x;\n"
        "[Function total(n) r]\nif $n>0 {let prev=call total($n-1); let r=$n+$prev;} else {let r=0;}\n"
        "[Function fresh(set) r]\nif $set {let r=7;}\n"
        "[Function order(a,b)]\nvar(6):=$a;\nvar(7):=$b;\n"
        "[Function identity(x) r]\nlet r=$x;\n"
        "[Function loop()]\nfor j=0;4 {if $j=1 {continue;} if $j=3 {break;} var(9):=var(9)+1;}\n"
        "[Function empty()]\n";
    const char *bad[]={
        "[Function f(a,a)]\n", "[Function f(a) a]\n", "[Function f() _]\n",
        "[Function f(a,)]\n", "[Function f() a,]\n", "[Function f(a b)]\n",
        "[Function f() a b]\n", "[Function f.oo()]\n", "[Function f($a)]\n",
        "[Function f()]\n[Function F()]\n", "[Function f()]\nbreak;\n",
        "[Function f()]\ncontinue;\n", "[Function f()]\npersistent(1){}\n",
        "[StateDef 0]\ncall f;\n", "[StateDef 0]\ncall f(1,);\n",
        "[StateDef 0]\ncall f(,1);\n", "[StateDef 0]\ncall f(1\n",
        "[StateDef 0]\ncall f()\n", "[StateDef 0]\nlet a,a=call f();\n",
        "[StateDef 0]\ncall f($missing);\n", "[Function f()]\nlet x=$other;\n",
        "[Function f()] Null{}\n", "[Function f()]\n[StateDef 0]\nvar(0):=$a;\n"
    };
    ZssProgram *program=NULL;
    ZssDiagnostic d;
    int calls, i, result, warnings;
    size_t j;
    memset(player.var,0,sizeof(player.var));memset(player.fvar,0,sizeof(player.fvar));
    player.fvar[31]=NAN;
    if (!Zss_CompileProgram(source,strlen(source),&owner,&program,&d)) {
        fprintf(stderr,"function fixture %u:%u: %s\n",(unsigned)d.line,(unsigned)d.column,d.message);
        abort();
    }
    run_function_program(program);
    if (player.var[2]!=15 || player.var[3]!=7 || player.var[4]!=73 || player.var[5]!=2)
        fprintf(stderr,"function results sum=%d fresh=%d caller=%d effects=%d\n",
                player.var[2],player.var[3],player.var[4],player.var[5]);
    assert(player.var[0]==7 && player.fvar[0]==1.5f && player.var[1]==2);
    assert(player.var[2]==15 && player.var[3]==7 && player.var[4]==73 && player.var[5]==2);
    assert(player.var[6]==1 && player.var[7]==4 && player.var[8]==INT_MIN);
    assert(player.var[9]==6 && player.var[10]==123);
    /* Per-call allocation failures unwind all frames and do not leak into the
     * next invocation. Successful completion alone may publish return values. */
    zss_calloc_calls=0;run_function_program(program);calls=zss_calloc_calls;
    for(i=1;i<=calls;i++) {
        zss_calloc_calls=0;zss_fail_call=i;result=0;
        assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
        assert(result&STATECTRL_STOP);zss_fail_call=0;
        assert(g_exprStackTop==-1);
    }
    run_function_program(program);Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    for(j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        assert(!Zss_CompileProgram(bad[j],strlen(bad[j]),&owner,&program,&d));
        assert(!program && d.message);zss_no_allocs();
    }
    /* Undefined/arity failures are no-ops before argument evaluation. */
    source="[StateDef 0]\nlet a=44;\ncall missing(var(12):=99);\ncall one(var(12):=88,2);\nlet a,b=call one(var(12):=77);\nvar(13):=$a;\nfor i=0;9 {call missing();}\n[Function one(x) r]\nlet r=$x;\n";
    warnings=player_warnings;player.var[12]=0;
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    run_function_program(program);
    assert(!player.var[12] && player.var[13]==44 && player_warnings-warnings==4);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    /* A stop in a nested call skips all callers and does not resume old code. */
    source="[StateDef 0]\ncall outer();\nvar(14):=99;\n[Function outer()]\ncall inner();\nvar(14):=88;\n[Function inner()]\nCtrlSet{value:7}\nvar(14):=77;\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    visits=0;stop_visit=1;player.var[14]=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(result==(STATECTRL_STOP|STATECTRL_CHANGED) && visits==1 && !player.var[14]);
    stop_visit=0;Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    source="[StateDef 0]\ncall recur();\nvar(15):=99;\n[Function recur()]\ncall recur();\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    warnings=player_warnings;player.var[15]=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(result&STATECTRL_STOP);assert(!player.var[15] && player_warnings==warnings+1);
    Zss_FreeProgram(program);zss_no_allocs();
    /* Every compiler allocation, including partially compiled call operands,
     * is owned by the program until publication or rollback. */
    source="[StateDef 0]\nlet a,b=call pair(var(0)+1,cond(var(1),2,3));\n[Function pair(x,y) a,b]\nlet a=$x;\nlet b=$y;\n";
    program=NULL;zss_calloc_calls=0;
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));calls=zss_calloc_calls;
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    for(i=1;i<=calls;i++) {
        zss_calloc_calls=0;zss_fail_call=i;
        assert(!Zss_CompileProgram(source,strlen(source),&owner,&program,&d) && !program);
        zss_fail_call=0;zss_no_allocs();
    }
    puts("ok: function headers, fresh numeric frames, calls/returns, recursion, stop propagation and OOM rollback");
}
static void test_zss_function_library(void)
{
    const char *caller="[StateDef 65000]\nvar(20):=0;\nlet value=call future();\nif $value {var(20):=$value;}\n[Function first() r]\nlet r=41;\n";
    const char *functions="[Function future() value]\nlet value=call first();\n[Function first() r]\nlet r=77;\n";
    const char *duplicate="[Function first() r]\nlet r=999;\n";
    ZssProgram *state=NULL, *module=NULL, *other=NULL;
    ZssDiagnostic d;
    StateDefs *defs, *wrong_owner;
    const char *error;
    Stream stream={0};
    KeyedArray *old;
    int i, calls;
    assert(Zss_CompileProgram(caller,strlen(caller),&owner,&state,&d));
    assert(Zss_CompileProgram(functions,strlen(functions),&owner,&module,&d));
    assert(Zss_CompileProgram(duplicate,strlen(duplicate),&owner,&other,&d));
    defs=empty_function_defs();assert(Zss_InstallProgram(state,defs,&error));
    run_function_bound(defs,65000);assert(!player.var[20]);
    zss_calloc_calls=0;
    assert(Zss_InstallProgram(module,defs,&error));calls=zss_calloc_calls;
    assert(Zss_InstallProgram(other,defs,&error));
    wrong_owner=empty_function_defs();assert(Zss_InstallProgram(other,wrong_owner,&error));
    player.states=player.custom_states=wrong_owner;
    run_function_bound(defs,65000);assert(player.var[20]==41);
    player.states=player.custom_states=NULL;
    StateDefs_Free(wrong_owner);StateDefs_Free(defs);
    for(i=1;i<=calls;i++) {
        defs=empty_function_defs();assert(Zss_InstallProgram(state,defs,&error));old=defs->list;
        zss_calloc_calls=0;zss_fail_call=i;
        assert(!Zss_InstallProgram(module,defs,&error) && defs->list==old);
        zss_fail_call=0;
        run_function_bound(defs,65000);assert(!player.var[20]);
        assert(Zss_InstallProgram(module,defs,&error));
        run_function_bound(defs,65000);assert(player.var[20]==41);
        StateDefs_Free(defs);
    }
    /* Function-only primary sources retain their library without a dummy
     * playable state. A later caller resolves through that same library. */
    stream.type=STREAM_MEMORY;stream.begin=stream.pos=(void *)functions;stream.end=stream.begin+strlen(functions);
    defs=Zss_LoadStateStreamNew(&stream,&owner,&d);assert(defs);
    assert(KeyedArray_Find(defs->list,0,0)==-1 && Zss_InstallProgram(state,defs,&error));
    Zss_FreeProgram(state);Zss_FreeProgram(module);Zss_FreeProgram(other);
    run_function_bound(defs,65000);assert(player.var[20]==77);
    StateDefs_Free(defs);zss_no_allocs();
    puts("ok: function-only files, forward cross-file calls, first-wins lookup, custom-state ownership and atomic library append");
}
static void test_zss_function_limits(void)
{
    char text[16384];
    size_t used;
    unsigned j;
    int result, warnings, calls, i;
    ZssProgram *program=NULL;
    ZssDiagnostic d;
    StateDefs *defs;
    KeyedArray *old;
    const char *error, *source;
    Expr legacy={0};
    const char *end;
    used=(size_t)snprintf(text,sizeof(text),"[Function many(");
    for(j=0;j<256;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"%sa%u",j ? "," : "",j);
    used+=(size_t)snprintf(text+used,sizeof(text)-used,")]\nvar(16):=$a255;\n[StateDef 0]\ncall many(");
    for(j=0;j<256;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"%s%u",j ? "," : "",j);
    used+=(size_t)snprintf(text+used,sizeof(text)-used,");\n");assert(used<sizeof(text));
    assert(Zss_CompileProgram(text,used,&owner,&program,&d));
    run_function_program(program);assert(player.var[16]==255);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    used=(size_t)snprintf(text,sizeof(text),"[Function many(");
    for(j=0;j<257;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"%sa%u",j ? "," : "",j);
    used+=(size_t)snprintf(text+used,sizeof(text)-used,")]\n");
    assert(!Zss_CompileProgram(text,used,&owner,&program,&d));zss_no_allocs();
    /* 256 result slots, including destination index 255, are not uint8 counts. */
    used=(size_t)snprintf(text,sizeof(text),"[StateDef 0]\nlet ");
    for(j=0;j<256;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"%sr%u",j ? "," : "",j);
    used+=(size_t)snprintf(text+used,sizeof(text)-used,"=call many();\nvar(16):=$r255;\n[Function many() ");
    for(j=0;j<256;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"%sr%u",j ? "," : "",j);
    used+=(size_t)snprintf(text+used,sizeof(text)-used,"]\nlet r255=17;\n");assert(used<sizeof(text));
    assert(Zss_CompileProgram(text,used,&owner,&program,&d));
    run_function_program(program);assert(player.var[16]==17);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    used=(size_t)snprintf(text,sizeof(text),"[Function many(");
    for(j=0;j<256;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"%sa%u",j ? "," : "",j);
    used+=(size_t)snprintf(text+used,sizeof(text)-used,") result]\n");
    assert(!Zss_CompileProgram(text,used,&owner,&program,&d));zss_no_allocs();
    used=0;
    for(j=0;j<64;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"[Function f%u()]\n",j);
    assert(Zss_CompileProgram(text,used,&owner,&program,&d));Zss_FreeProgram(program);program=NULL;
    used+=(size_t)snprintf(text+used,sizeof(text)-used,"[Function f64()]\n");
    assert(!Zss_CompileProgram(text,used,&owner,&program,&d));zss_no_allocs();
    /* Calls cannot reset the total-work guard, or exhaust the C stack by
     * nesting block stacks below a recursion guard. */
    source="[StateDef 0]\nfor i=0;2499 {call work();}\nvar(17):=99;\n[Function work()]\nfor j=0;2499 {var(18):=var(18)+1;}\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    player.var[17]=player.var[18]=0;warnings=player_warnings;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(result&STATECTRL_STOP);assert(!player.var[17] && player.var[18]>2500 && player_warnings>warnings);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    source="[Function f()]\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    defs=empty_function_defs();
    for(j=0;j<64;j++) assert(Zss_InstallProgram(program,defs,&error));
    old=defs->list;assert(!Zss_InstallProgram(program,defs,&error) && defs->list==old);
    StateDefs_Free(defs);Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    used=(size_t)snprintf(text,sizeof(text),"[StateDef 0]\ncall deep();\n[Function deep()]\n");
    for(j=0;j<32;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"if 1 {");
    used+=(size_t)snprintf(text+used,sizeof(text)-used,"call deep();");
    for(j=0;j<32;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"}");
    assert(Zss_CompileProgram(text,used,&owner,&program,&d));warnings=player_warnings;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(result&STATECTRL_STOP);assert(player_warnings==warnings+1);
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    /* Initial metadata/library publication must also roll back atomically. */
    source="[StateDef 65000]\nlet r=call pick();\n[Function pick() r]\nlet r=7;\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    defs=empty_function_defs();zss_calloc_calls=0;
    assert(Zss_InstallProgram(program,defs,&error));calls=zss_calloc_calls;StateDefs_Free(defs);
    for(i=1;i<=calls;i++) {
        defs=empty_function_defs();old=defs->list;zss_calloc_calls=0;zss_fail_call=i;
        assert(!Zss_InstallProgram(program,defs,&error) && defs->list==old && !old->count);
        zss_fail_call=0;StateDefs_Free(defs);
    }
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    /* Extending ZSS's delimiter support must not change the CNS profile. */
    source="1=[1,2],3";end=source;
    assert(!Expr_Compile(source,&legacy,&owner,&end,EXPR_WANT_ANY));Expr_Free(&legacy);zss_no_allocs();
    /* Parse-only redirect boundary: runtime player targeting is exercised by
     * engine integration, not the host var-trigger stub. */
    source="[StateDef 0]\ncall f(parent,var(0),cond(1,2,3),1=(0,2]);\n[Function f(a,b,c)]\n";
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    used=(size_t)snprintf(text,sizeof(text),"[Function ");
    memset(text+used,'f',63);used+=63;strcpy(text+used,"()]\n");used+=4;
    assert(Zss_CompileProgram(text,used,&owner,&program,&d));
    Zss_FreeProgram(program);program=NULL;zss_no_allocs();
    used=(size_t)snprintf(text,sizeof(text),"[Function ");
    memset(text+used,'f',64);used+=64;strcpy(text+used,"()]\n");used+=4;
    assert(!Zss_CompileProgram(text,used,&owner,&program,&d));zss_no_allocs();
    puts("ok: 256-slot/call boundaries, shared fuel/frame guards, initial library rollback and unchanged CNS intervals");
}
