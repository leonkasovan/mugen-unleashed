/* CNS/controller ABI regressions. Production parsers and Expr evaluator run.
 * INI lookup, warning output and var-trigger evaluation are explicit boundaries.
 * Including the units exposes their private controller/math helpers to tests;
 * unused game-controller branches are discarded by the linker. */
#include "mugen.h"
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
/* Run from the CRT directly; no SDLmain library is linked by this harness. */
#ifdef main
#undef main
#endif
#include "test_abi.h"
#ifdef TEST_EXPECT_BITS
_Static_assert(sizeof(void *) * 8 == TEST_EXPECT_BITS, "wrong CNS harness ABI");
#endif

static void *tracked[512];
static int alloc_calls, fail_alloc;
static int expect_exit;
static jmp_buf exit_jump;
static const char *key_names[8], *key_values[8];
static int key_count, ini_reads;
static char clipboard[8192];
static GameVars game;
static Player player;
static CharData owner;

static void audit_free(void *p)
{
    int i;
    for (i = 0; i < 512; i++) if (tracked[i] == p) tracked[i] = NULL;
    free(p);
}
void *Mem_Calloc(size_t n, size_t size)
{
    void *p;
    int i;
    alloc_calls++;
    if (alloc_calls == fail_alloc) { g_outOfMemory = 1; return NULL; }
    p = calloc(n, size);
#ifdef TEST_ZSS_CONTROLLER
    if (p == NULL) {g_outOfMemory=1;return NULL;}
#else
    assert(p);
#endif
    for (i = 0; i < 512; i++) if (!tracked[i]) { tracked[i] = p; return p; }
    abort();
}
static void check_no_allocs(void)
{
    int i;
    for (i = 0; i < 512; i++) assert(!tracked[i]);
}
void Error_Printf(const char *fmt, ...) { (void)fmt; }
void Error_Exit(void) { if (expect_exit) longjmp(exit_jump, 1); abort(); }
void Error_Fatal(const char *fmt, ...) { (void)fmt; abort(); }
void Error_OutOfMemory(void) { abort(); }
static int player_warnings, random_reads;
void Player_Warn(Player *p, const char *msg) { (void)p; (void)msg; player_warnings++; }
void Player_WarnAt(Player *p, const StateCtrl *sc, const char *fmt, ...) { (void)p; (void)sc; (void)fmt; }
void TextRing_SetPrintf(TextRing *ring, const char *fmt, ...)
{
    va_list ap;
    (void)ring;
    va_start(ap, fmt); vsnprintf(clipboard, sizeof(clipboard), fmt, ap); va_end(ap);
}
void TextRing_Printf(TextRing *ring, const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(clipboard);
    (void)ring;
    va_start(ap, fmt); vsnprintf(clipboard + n, sizeof(clipboard) - n, fmt, ap); va_end(ap);
}
#ifndef TEST_ZSS_CONTROLLER
char *Ini_GetValue(Ini *ini, const char *key)
{
    int i;
    (void)ini;
    for (i = 0; i < key_count; i++) if (!_stricmp(key, key_names[i])) return (char *)key_values[i];
    return NULL;
}
char *Ini_GetNextValue(Ini *ini, const char *key) { (void)ini; (void)key; return NULL; }
const char *Ini_FirstLine(Ini *ini) { (void)ini; return NULL; }
const char *Ini_NextLine(Ini *ini) { (void)ini; return NULL; }
void Ini_Rewind(Ini *ini) { (void)ini; }
int Ini_SelectGroup(Ini *ini, const char *group) { (void)ini; (void)group; ini_reads++; return 0; }
int Ini_CountGroups(Ini *ini, const char *prefix) { (void)ini; (void)prefix; return 0; }
const char *Ini_NextGroup(Ini *ini) { (void)ini; return NULL; }
int Ini_SourceLine(int line, Ini *ini) { (void)line; (void)ini; return 0; }
#endif
/* These parse branches are outside the cases exercised by this harness. */
int Trans_Parse(const char *s, int *dst, int *src) { (void)s; (void)dst; (void)src; abort(); }
float Units_From320(float v) { (void)v; abort(); }
int Ini_ReadString(Ini *ini, const char *key, unsigned int size, char *out)
{ (void)ini; (void)key; (void)size; (void)out; abort(); }
static void test_trigger_eval(Player *p, const Trigger *t);
static void test_trigger_eval_ctx(Player *p, const Trigger *t, ExprEvalCtx *ctx);

#define free audit_free
#include "../src/trigger.c"
#define Trigger_EvalCtx test_trigger_eval_ctx
#include "../src/expr.c"
#undef free
#undef Trigger_EvalCtx
#include "../src/sctrl.c"
#include "../src/statedef.c"

static void test_trigger_eval(Player *p, const Trigger *t)
{
    int idx;
    assert((t->id == TRIGGER_VAR || t->id == TRIGGER_FVAR) && t->redirect == TRIGGER_REDIRECT_NONE);
    assert(Trigger_PopInt(p, &idx) != EXPR_BOTTOM);
    if (t->id==TRIGGER_FVAR) {
        assert(idx>=0 && idx<PLAYER_FVAR_COUNT);
        ExprStack_Push(float_bits(p->fvar[idx]),EXPR_FLOAT);
    } else {
        assert(idx >= 0 && idx < PLAYER_VAR_COUNT);
        ExprStack_Push(p->var[idx], EXPR_INT);
    }
}
static void test_trigger_eval_ctx(Player *p, const Trigger *t, ExprEvalCtx *ctx)
{
    if (t->id==TRIGGER_RANDOM && t->redirect==TRIGGER_REDIRECT_NONE) {
        random_reads++;ExprStack_Push(123,EXPR_INT);return;
    }
    if (t->id==TRIGGER_COND && t->redirect==TRIGGER_REDIRECT_NONE) {
        Trigger_EvalCondCtx(p,t,ctx);return;
    }
    test_trigger_eval(p,t);
}
static void keys(const char *type, const char *range)
{
    key_count = 0;
    key_names[key_count] = "type"; key_values[key_count++] = type;
    if (range) {
        key_names[key_count] = "v"; key_values[key_count++] = "0";
        key_names[key_count] = "range"; key_values[key_count++] = range;
    }
}
static void test_controller_parse(void)
{
    StateCtrl sc = {0};
    Ini ini = {0};
    char long_type[1024];
    memset(long_type, 'x', sizeof(long_type) - 1); long_type[sizeof(long_type) - 1] = 0;
    keys(long_type, NULL);
    assert(!StateCtrl_ParseParams(&ini, &sc, &owner));
    keys("varrandom", "var(1) + 3");
    assert(StateCtrl_ParseParams(&ini, &sc, &owner));
    player.var[1] = 9;
    assert(Expr_EvalInt(&player, &sc.arg[0], 0) == 0);
    assert(Expr_EvalInt(&player, &sc.arg[2], 0) == 12);
    StateCtrl_Free(&sc);
    check_no_allocs();
    puts("ok: overlong type rejected; VarRandom moves computed range ownership");
}
static void test_variables(void)
{
    StateCtrl sc = {0};
    Expr_SetInt(&sc.arg[0], 1); Expr_SetInt(&sc.arg[1], 0);
    Expr_SetInt(&sc.arg[2], SCTRL_VAR);
    player.var[0] = INT_MAX;
    run_var(&player, &sc, 1); assert(player.var[0] == INT_MIN);
    Expr_SetInt(&sc.arg[2], SCTRL_SYSVAR);
    player.sysvar[0] = INT_MAX;
    run_var(&player, &sc, 1); assert(player.sysvar[0] == INT_MIN);
    Expr_SetInt(&sc.arg[2], 0);
    player.helper = 1; player.var[0] = INT_MAX;
    run_parent_var(&player, &sc, &player, 1); assert(player.var[0] == INT_MIN);
    assert(var_random_value(INT_MIN, INT_MAX, 0) == INT_MIN);
    assert(var_random_value(INT_MIN, INT_MAX, INT_MAX) == -1);
    assert(var_random_value(INT_MAX, INT_MAX, 42) == INT_MAX);
    assert(var_random_value(2, 7, 11) == 7);
    puts("ok: int32 wrapping variable additions and full-range random bounds");
}
static void test_clipboard(void)
{
    StateCtrl sc = {0};
    sc.type = SCTRL_DISPLAYTOCLIPBOARD;
    sc.params = CnsText_Parse("\"100%% %%n\""); assert(sc.params);
    run_clipboard(&player, &sc, NULL); assert(!strcmp(clipboard, "100% %n")); free(sc.params);
    sc.params = CnsText_Parse("\"value=%d / %.2f%%\""); assert(sc.params);
    Expr_SetInt(&sc.arg[0], 7); Expr_SetFloat(&sc.arg[1], 1.5f);
    run_clipboard(&player, &sc, NULL); assert(!strcmp(clipboard, "value=7 / 1.50%"));
    sc.type = SCTRL_APPENDTOCLIPBOARD;
    run_clipboard(&player, &sc, NULL); assert(!strcmp(clipboard, "value=7 / 1.50%value=7 / 1.50%"));
    free(sc.params);
    sc.type = SCTRL_DISPLAYTOCLIPBOARD;
    sc.params = CnsText_Parse("\"left\\q%d\""); assert(sc.params);
    run_clipboard(&player, &sc, NULL); assert(!strcmp(clipboard, "left7")); free(sc.params);
    sc.params = CnsText_Parse("\"%99999d\""); assert(sc.params);
    run_clipboard(&player, &sc, NULL); assert(strlen(clipboard) == 4095); free(sc.params);
    assert(!CnsText_Parse("\"bad\\"));
    puts("ok: clipboard percent literals, numeric parameters, truncation, malformed escape");
}
static void test_expr_allocs(void)
{
    Expr src = {0}, dst = {0};
    int fail;
    for (fail = 1; fail <= 2; fail++) {
        Expr_SetInt(&dst, 77);
        alloc_calls = 0; fail_alloc = fail;
        assert(!Expr_Compile("1+2", &dst, &owner, NULL, EXPR_WANT_INT));
        assert(dst.count == EXPR_CONST && dst.u.value.i == 77);
        check_no_allocs(); fail_alloc = 0;
    }
    assert(Expr_Compile("var(1)+3", &src, &owner, NULL, EXPR_WANT_INT));
    assert(Expr_Copy(&src, &src));
    for (fail = 1; fail <= 2; fail++) {
        alloc_calls = 0; fail_alloc = fail;
        assert(!Expr_Copy(&dst, &src));
        assert(dst.count == EXPR_CONST && dst.u.value.i == 77);
        fail_alloc = 0;
    }
    alloc_calls = 0; fail_alloc = 1; expect_exit = 1;
    if (setjmp(exit_jump) == 0) {
        Expr_Clone(&dst, &src);
        assert(!"clone allocation failure must reach Error_Exit");
    }
    expect_exit = 0; fail_alloc = 0;
    assert(dst.count == EXPR_CONST && dst.u.value.i == 77);
    assert(Expr_Clone(&dst, &src));
    assert(dst.u.tokens != src.u.tokens && dst.t.types != src.t.types);
    Expr_Free(&src); Expr_Free(&dst); check_no_allocs();
    {
        char long_expr[1024];
        ExprValue tokens[512];
        char types[512];
        int i;
        for (i = 0; i < 300; i++) { long_expr[i * 2] = '1'; long_expr[i * 2 + 1] = '+'; }
        long_expr[599] = 0;
        assert(!Expr_Parse(long_expr, tokens, types, &owner, 512, NULL, EXPR_WANT_ANY));
        check_no_allocs();
    }
    puts("ok: expression allocation, ownership and fixed-buffer token capacity");
}
static void test_trigger_copy_math(void)
{
    Expr src = {0}, dst = {0};
    assert(Expr_Compile("helper(var(1)+2), time", &src, &owner, NULL, EXPR_WANT_ANY));
    assert(Expr_Clone(&dst, &src)); Expr_Free(&src); Expr_Free(&dst); check_no_allocs();
    assert(Expr_Compile("authorname = \"Dhani\"", &src, &owner, NULL, EXPR_WANT_ANY));
    assert(Expr_Clone(&dst, &src));
    assert(src.u.tokens[0].trig->data != dst.u.tokens[0].trig->data);
    Expr_Free(&src); Expr_Free(&dst); check_no_allocs();
    assert(trigger_abs_int(INT_MIN) == INT_MIN);
    assert(trigger_abs_int(-42) == 42);
    assert(trigger_rounded_int(&player, TRIGGER_FLOOR, 2147483648.0f) == INT_MIN);
    assert(trigger_rounded_int(&player, TRIGGER_CEIL, NAN) == INT_MIN);
    assert(trigger_rounded_int(&player, TRIGGER_FLOOR, INFINITY) == INT_MAX);
    assert(trigger_rounded_int(&player, TRIGGER_FLOOR, -1.5f) == -2);
    {
        static const char *bad[] = {
            "helper(var(1)+2), no_such_trigger",
            "helper(var(1)+2) time", "(helper(var(1)+2), time",
            "sin(helper(var(1)+2), time, 2)",
            "1 = [helper(var(1)+2), time, no_such_trigger]"
        };
        size_t i;
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
            assert(!Expr_Compile(bad[i], &src, &owner, NULL, EXPR_WANT_ANY));
            Expr_Free(&src); check_no_allocs();
        }
    }
    puts("ok: trigger clones, rejected nested arguments and int32 math edge cases");
}
static void test_state_headers(void)
{
    static const char *bad[] = { "", "[", "[Statedef ]", "[Statedef 1x]", "[Statedef 2147483648]", "[Statedef -2147483649]" };
    StateDef sd;
    Ini ini = {0};
    size_t i;
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        ini_reads = 0;
        memset(&sd, 0xA5, sizeof(sd));
        assert(!StateDef_Parse(bad[i], &sd, &ini, &owner));
        assert(!sd.ctrls && !sd.source && ini_reads == 0);
    }
    {
        static const char *valid[] = { "[Statedef 0]", "[Statedef 2147483647]", "[Statedef -2147483648]" };
        for (i = 0; i < sizeof(valid)/sizeof(valid[0]); i++) {
            ini_reads = 0;
            /* The empty INI fixture then rejects the missing controller. */
            assert(!StateDef_Parse(valid[i], &sd, &ini, &owner));
            assert(ini_reads > 0);
        }
    }
    puts("ok: state headers rejected before offset reads and allocations");
}
#ifdef TEST_ZSS_CONTROLLER
/* Link wrappers cover constructor/temporary allocations too, including OOM. */
static void *zss_allocs[1024];
static int zss_calloc_calls, zss_fail_call;
void *__real_calloc(size_t n,size_t size);
void __real_free(void *p);
void *__wrap_calloc(size_t n,size_t size)
{
    void *p;int i;
    if(++zss_calloc_calls==zss_fail_call) return NULL;
    p=__real_calloc(n,size);if(!p)return NULL;
    for(i=0;i<1024;i++)if(!zss_allocs[i]){zss_allocs[i]=p;return p;}
    abort();
}
void __wrap_free(void *p)
{
    int i;
    for(i=0;i<1024;i++)if(zss_allocs[i]==p){zss_allocs[i]=NULL;break;}
    __real_free(p);
}
static void zss_no_allocs(void)
{
    int i;check_no_allocs();for(i=0;i<1024;i++)assert(!zss_allocs[i]);
}
#include "scriptparams.h"
#include "zss_controller.h"
#include "zss_statedef.h"
#include "zss_program.h"
#include "zss_binding.h"
/* Stream I/O is the explicit boundary; compilation/installation are real. */
size_t Stream_Read(void *out,size_t size,size_t count,Stream *s)
{
    size_t available=(size_t)(s->end-s->pos), bytes=size*count;
    if(bytes>available){bytes=available;g_streamError=STREAM_ERR_EOF;}
    memcpy(out,s->pos,bytes);s->pos+=bytes;return bytes/size;
}
static void test_zss_installation(void)
{
    const char *source="[StateDef 65000;anim:2+3;]\nCtrlSet{value:1}\n[StateDef -2]\nNull{}\n";
    ZssProgram *program=NULL;
    ZssDiagnostic d;
    StateDefs *defs;
    StateDef *sd;
    const char *error;
    int calls,i,slot;
    Stream stream={0};
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    defs=calloc(1,sizeof(*defs));assert(defs);defs->list=KeyedArray_Create(1,sizeof(StateDef));assert(defs->list);
    zss_calloc_calls=0;
    assert(Zss_InstallProgram(program,defs,&error));calls=zss_calloc_calls;
    assert(defs->list->count==2);
    slot=KeyedArray_Find(defs->list,65000,0);assert(slot>=0);
    sd=(StateDef *)(defs->list->data+defs->list->elem_size*slot);
    assert(Zss_IsBoundState(sd) && Expr_EvalInt(&player,&sd->anim,0)==5);
    assert(!Zss_InstallProgram(program,defs,&error) && error && defs->list->count==2);
    StateDefs_Free(defs);
    /* Caller reference still owns the program after a collection is destroyed. */
    assert(Zss_ProgramStateCount(program)==2);
    for(i=1;i<=calls;i++) {
        KeyedArray *old;
        defs=calloc(1,sizeof(*defs));assert(defs);defs->list=KeyedArray_Create(1,sizeof(StateDef));assert(defs->list);
        old=defs->list;zss_calloc_calls=0;zss_fail_call=i;
        assert(!Zss_InstallProgram(program,defs,&error));
        assert(defs->list==old && !old->count);zss_fail_call=0;StateDefs_Free(defs);
    }
    {
        StateDef existing={0};
        StateCtrl no_op={0};
        KeyedArray *old;
        defs=calloc(1,sizeof(*defs));assert(defs);defs->list=KeyedArray_Create(1,sizeof(StateDef));assert(defs->list);
        existing.ctrls=KeyedArray_Create(1,sizeof(StateCtrl));assert(existing.ctrls);
        no_op.type=SCTRL_NOP;assert(KeyedArray_Add(existing.ctrls,&no_op,SCTRL_NOP,0)>=0);
        assert(KeyedArray_Add(defs->list,&existing,42,0)>=0);
        assert(Zss_InstallProgram(program,defs,&error) && defs->list->count==3);
        slot=KeyedArray_Find(defs->list,42,0);assert(slot>=0);
        sd=(StateDef *)(defs->list->data+defs->list->elem_size*slot);
        assert(sd->ctrls==existing.ctrls && !Zss_IsBoundState(sd));
        old=defs->list;assert(!Zss_InstallProgram(program,defs,&error) && defs->list==old);
        StateDefs_Free(defs);
    }
    Zss_FreeProgram(program);zss_no_allocs();
    defs=calloc(1,sizeof(*defs));assert(defs);defs->list=KeyedArray_Create(1,sizeof(StateDef));assert(defs->list);
    stream.type=STREAM_MEMORY;stream.begin=stream.pos=(void *)source;stream.end=stream.begin+strlen(source);
    assert(Zss_LoadStateStream(&stream,defs,&owner,&d));
    assert(defs->list->count==2);StateDefs_Free(defs);zss_no_allocs();
    puts("ok: atomic ZSS state installation, retained ownership and Stream loading");
}
static int visited[16], visits, stop_visit;
static int record_zss_controller(Player *p, StateCtrl *sc, ExprEvalCtx *frame, void *context)
{
    assert(context==&owner && sc->type==SCTRL_CTRLSET && visits<16);
    visited[visits++]=Expr_EvalIntCtx(p,&sc->arg[0],0,frame);
    p->var[0]=visited[visits-1];
    return visits==stop_visit ? STATECTRL_STOP|STATECTRL_CHANGED : 0;
}
static void test_zss_loops(void)
{
    const char *source="[StateDef 0]\nvar(0):=0;\nwhile var(0)<6 { var(0):=var(0)+1; if var(0)=2 { continue; } if var(0)=4 { break; } }\nvar(1):=var(0);\nfvar(0):=1.25;\n";
    const char *nested="[StateDef 0]\nvar(0):=0;\nvar(1):=0;\nwhile var(0)<3 { var(0):=var(0)+1; var(2):=0; while var(2)<5 { var(2):=var(2)+1; if var(2)=2 {continue;} if var(2)=4 {break;} var(1):=var(1)+1; } }\n";
    const char *limit="[StateDef 0]\nvar(0):=0;\nwhile 1 {var(0):=var(0)+1;}\nvar(1):=9;\n";
    const char *fuel="[StateDef 0]\nwhile 1 {while 1 {}}\nCtrlSet{value:9}\n";
    const char *stop="[StateDef 0]\nwhile 1 {CtrlSet{value:1}}\nCtrlSet{value:2}\n";
    const char *bad[]={"[StateDef 0]\nbreak;", "[StateDef 0]\ncontinue;", "[StateDef 0]\nvar(0):=1", "[StateDef 0]\n1+2;", "[StateDef 0]\nwhile 1 {break}", "[StateDef 0]\nwhile 1 {continue 2;}", "[StateDef 0]\nwhile var(0) && {}"};
    ZssProgram *program=NULL, *failed=NULL;
    ZssDiagnostic d;
    int result, calls;
    size_t i;
    visits=stop_visit=player_warnings=0;
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && !visits && player.var[0]==4 && player.var[1]==4 && player.fvar[0]==1.25f && !player_warnings);
    /* Execution frames are fresh on every call, not stored in shared nodes. */
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));assert(player.var[0]==4);
    Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(nested,strlen(nested),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(player.var[0]==3 && player.var[1]==6 && player.var[2]==4);Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(limit,strlen(limit),&owner,&program,&d));player_warnings=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && player.var[0]==2500 && player.var[1]==9 && player_warnings==1);
    player_warnings=0;assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(player.var[0]==2500 && player_warnings==1);Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(fuel,strlen(fuel),&owner,&program,&d));visits=player_warnings=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert((result&STATECTRL_STOP) && !(result&STATECTRL_CHANGED) && !visits && player_warnings>=1);Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(stop,strlen(stop),&owner,&program,&d));visits=0;stop_visit=1;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(visits==1 && (result&STATECTRL_STOP) && (result&STATECTRL_CHANGED));Zss_FreeProgram(program);zss_no_allocs();stop_visit=0;
    for(i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        assert(!Zss_CompileProgram(bad[i],strlen(bad[i]),&owner,&failed,&d));assert(!failed && d.message);zss_no_allocs();
    }
    zss_calloc_calls=0;assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));calls=zss_calloc_calls;Zss_FreeProgram(program);
    for(i=1;i<=(size_t)calls;i++) {
        zss_calloc_calls=0;zss_fail_call=(int)i;
        if(Zss_CompileProgram(source,strlen(source),&owner,&failed,&d)){Zss_FreeProgram(failed);failed=NULL;}
        assert(!failed);zss_no_allocs();
    }
    zss_fail_call=0;
    puts("ok: assignments, nested while, break/continue, loop guard and total fuel");
}
static void test_zss_ranges(void)
{
    const struct {const char *range;int count, warnings;} cases[]={
        {"0;3",4,0}, {"3;0;-1",4,0}, {"0;5;2",3,0},
        {"5;0;-2",3,0}, {"0;0",1,0}, {"3;0",0,0}, {"0;3;-1",0,0},
        {"0;3;0",2500,1}, {"3;0;0",2500,1}, {"0;2499",2500,0},
        {"0;2500",2500,1}, {"0.9;2.9",3,0},
        {"2147483646;2147483647;1",2500,1},
        {"-2147483647;-2147483648;-1",2500,1}
    };
    const char *source="[StateDef 0]\nvar(0):=0;\nvar(1):=0;\nfor (var(1):=var(1)+1);(var(1):=var(1)+2);(var(1):=var(1)+1) {var(0):=var(0)+1;var(1):=99;}\n";
    const char *nested="[StateDef 0]\nvar(0):=0;\nvar(1):=0;\nfor 1;3 {var(0):=var(0)+1; for 0;9 {var(1):=var(1)+1; if var(1)%3=1 {continue;} if var(1)%3=0 {break;}}}\n";
    const char *stop="[StateDef 0]\nfor 0;3 {CtrlSet{value:1}}\nCtrlSet{value:2}\n";
    const char *bad[]={"for 0 {}", "for ;3 {}", "for 0; {}", "for 0;3; {}", "for 0;3;1;2 {}", "for let $i=0;3 {}", "for 0;3 {break}", "for 0;3", "for 0;3 {", "for 0;3 {} else {}"};
    char text[1024];
    ZssProgram *program=NULL, *failed=NULL;
    ZssDiagnostic d;
    size_t j;
    int result, calls;
    for(j=0;j<sizeof(cases)/sizeof(*cases);j++) {
        snprintf(text,sizeof(text),"[StateDef 0]\nvar(0):=0;\nfor %s {var(0):=var(0)+1;}\nvar(2):=7;\n",cases[j].range);
        if (!Zss_CompileProgram(text,strlen(text),&owner,&program,&d)) {fprintf(stderr,"range %s: %s at %u:%u\n",cases[j].range,d.message,d.line,d.column);assert(0);}
        player_warnings=0;player.var[2]=0;
        assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
        if(result || player.var[0]!=cases[j].count || player.var[2]!=7 || player_warnings!=cases[j].warnings)
            fprintf(stderr,"range %s: count=%d warnings=%d result=%d tail=%d\n",cases[j].range,player.var[0],player_warnings,result,player.var[2]);
        assert(!result && player.var[0]==cases[j].count && player.var[2]==7 && player_warnings==cases[j].warnings);
        Zss_FreeProgram(program);zss_no_allocs();
    }
    /* Begin/end/step execute once, left to right, even when the body mutates
     * their source vars. Every invocation gets a fresh counter and endpoints. */
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    for(j=0;j<2;j++) {
        assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
        assert(!result && player.var[0]==1 && player.var[1]==99);
    }
    Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(nested,strlen(nested),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && player.var[0]==3 && player.var[1]==9);Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(stop,strlen(stop),&owner,&program,&d));visits=0;stop_visit=1;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(visits==1 && (result&STATECTRL_STOP) && (result&STATECTRL_CHANGED));Zss_FreeProgram(program);zss_no_allocs();stop_visit=0;
    for(j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        snprintf(text,sizeof(text),"[StateDef 0]\n%s",bad[j]);
        assert(!Zss_CompileProgram(text,strlen(text),&owner,&failed,&d));assert(!failed && d.message);zss_no_allocs();
    }
    strcpy(text,"[StateDef 0]\nvar(0):=0;\nvar(1):=0;\nfor 0;3 {var(0):=var(0)+1;continue;var(1):=9;}\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));player_warnings=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && player.var[0]==4 && player.var[1]==0 && !player_warnings);Zss_FreeProgram(program);zss_no_allocs();
    strcpy(text,"[StateDef 0]\nfor 0;2499 {for 0;2499 {}}\nCtrlSet{value:9}\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));visits=player_warnings=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert((result&STATECTRL_STOP) && !(result&STATECTRL_CHANGED) && !visits && player_warnings==1);Zss_FreeProgram(program);zss_no_allocs();
    /* Signed minimum must survive token lowering in assignments, guards and
     * controller parameter values, not become unary negation of a clamped max. */
    strcpy(text,"[StateDef 0]\nvar(0):=-2147483648;\nif var(0)=-2147483648 {CtrlSet{value:-2147483648}}\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));visits=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && visits==1 && visited[0]==INT32_MIN);Zss_FreeProgram(program);zss_no_allocs();
    zss_calloc_calls=0;assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));calls=zss_calloc_calls;Zss_FreeProgram(program);
    for(j=1;j<=(size_t)calls;j++) {
        zss_calloc_calls=0;zss_fail_call=(int)j;
        if(Zss_CompileProgram(source,strlen(source),&owner,&failed,&d)){Zss_FreeProgram(failed);failed=NULL;}
        assert(!failed);zss_no_allocs();
    }
    zss_fail_call=0;
    puts("ok: anonymous inclusive for ranges, entry evaluation, integer wrap, guards and cleanup");
}
static int context_slot(void *user, const char *name, size_t length)
{
    assert(user==&owner);
    return length==1 && *name=='x' ? 0 : length==1 && *name=='y' ? 1 : -1;
}
static void test_expression_contexts(void)
{
    Expr expression={0}, copy={0};
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprValue values[2]={{0},{0}}, code[1]={{0}};
    char types[2]={EXPR_INT,EXPR_INT}, kinds[1]={EXPR_LOCAL};
    ExprEvalCtx frame={values,types,2};
    const char *end;
    char nested[256];
    int result, before=g_exprStackTop;
    float f;
    size_t j;
    values[0].i=3;values[1].i=1;player.var[3]=7;
    assert(!Expr_Compile("$x",&expression,&owner,NULL,EXPR_WANT_ANY));zss_no_allocs();
    assert(Expr_CompileCtx("$x+cond($y,var($x),$x*2)",&expression,&owner,&end,EXPR_WANT_ANY,&compile) && !*end && !compile.depth);
    assert(Expr_EvalCtx(&player,&expression,&result,&f,&frame)==1 && result==10 && g_exprStackTop==before);
    values[1].i=0;assert(Expr_EvalCtx(&player,&expression,&result,&f,&frame)==1 && result==9);
    assert(Expr_Clone(&copy,&expression));Expr_Free(&expression);
    values[0].i=4;values[1].i=1;player.var[4]=8;
    assert(Expr_EvalCtx(&player,&copy,&result,&f,&frame)==1 && result==12);
    Expr_Free(&copy);zss_no_allocs();
    assert(Expr_CompileCtx("$x",&expression,&owner,&end,EXPR_WANT_ANY,&compile));
    assert(!Expr_Eval(&player,&expression,&result,&f) && g_exprStackTop==before);
    types[0]=EXPR_FLOAT;values[0].f=1.25f;
    assert(Expr_EvalCtx(&player,&expression,&result,&f,&frame)==2 && f==1.25f);
    types[0]=EXPR_BOTTOM;values[0].i=0;
    assert(Expr_EvalCtx(&player,&expression,&result,&f,&frame)==3);
    Expr_Free(&expression);zss_no_allocs();
    assert(!Expr_CompileCtx("cond(1,$x,$unknown)",&expression,&owner,&end,EXPR_WANT_ANY,&compile) && !compile.depth);zss_no_allocs();
    for(j=0;j<70;j++) nested[j]='(';
    nested[70]='$';nested[71]='x';for(j=72;j<142;j++) nested[j]=')';nested[142]=0;
    assert(!Expr_CompileCtx(nested,&expression,&owner,&end,EXPR_WANT_ANY,&compile) && !compile.depth);zss_no_allocs();
    /* Invalid slot/type contexts fail without reading outside the frame or
     * leaving an operand on the legacy expression stack. */
    expression.count=1;expression.u.tokens=code;expression.t.types=kinds;
    code[0].i=256;assert(!Expr_EvalCtx(&player,&expression,&result,&f,&frame) && g_exprStackTop==before);
    code[0].i=0;types[0]=99;assert(!Expr_EvalCtx(&player,&expression,&result,&f,&frame) && g_exprStackTop==before);
    memset(&expression,0,sizeof(expression));
    puts("ok: explicit expression contexts, nested cond/arguments, clone ownership and invalid-frame guards");
}
static void test_zss_lazy(void)
{
    const struct {const char *text;int x,type,side,warnings,random;float value;} cases[]={
        {"0 && (var(1):=9)",0,1,0,0,0,0},
        {"1 || (var(1):=9)",0,1,0,0,0,1},
        {"2 || (var(1):=9)",0,1,0,0,0,2},
        {"2 && (var(1):=9)",0,1,9,0,0,9},
        {"var(0) && (var(1):=9)",0,1,0,0,0,0},
        {"var(0) || (var(1):=9)",7,1,0,0,0,7},
        {"var(0) && 2 && 3",1,1,0,0,0,1},
        {"var(0) || 2 || 3",0,1,0,0,0,1},
        {"2 || 3",0,1,0,0,0,1}, {"2 && 3",0,1,0,0,0,1},
        {"(2+3) || 7",0,1,0,0,0,1},
        {"cond(1,2,3) || 4",0,1,0,0,0,1},
        {"cond(1,cond(1,2,3),4) || 5",0,1,0,0,0,1},
        {"((var(0):=1) && 0) || (var(1):=8)",0,1,8,0,0,8},
        {"var(0) && .5",1,2,0,0,0,.5f},
        {"var(0) || .5",0,2,0,0,0,.5f}, {"0 || .5",0,1,0,0,0,1},
        {"(1/0) && (var(1):=9)",0,3,0,1,0,0},
        {"(1/0) || (var(1):=9)",0,1,9,1,0,9},
        {"0 && (1/0)",0,1,0,0,0,0}, {"1 || (1/0)",0,1,0,0,0,1},
        {"(var(0)=[0,2]) && (var(1):=9)",1,1,9,0,0,9},
        {"(var(0)=[3,4]) && (var(1):=9)",1,1,0,0,0,0},
        {"var(0) && random",0,1,0,0,0,0}, {"var(0) && random",1,1,0,0,1,123},
        {"var(0) || random",7,1,0,0,0,7}, {"var(0) || random",0,1,0,0,1,123},
        {"cond(1,7,(var(1):=9)) || 8",0,1,0,0,0,7},
        {"var(0) || cond(1,(var(1):=4),0)",7,1,0,0,0,7},
        {"var(0) || cond(1,(var(1):=4),0)",0,1,4,0,0,4}
    };
    const char *owned="cond($x,(var(1):=$y),$x||random) && (var(2):=3)";
    Expr e={0}, copy={0};
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprValue values[2]={{0},{0}};
    char types[2]={EXPR_INT,EXPR_INT};
    ExprEvalCtx frame={values,types,2};
    const char *end;
    char text[1024];
    int result,type,calls,before=g_exprStackTop,at,save;
    float f;
    size_t j, used;
    for(j=0;j<sizeof(cases)/sizeof(*cases);j++) {
        assert(Expr_CompileCtx(cases[j].text,&e,&owner,&end,EXPR_WANT_ANY,&compile) && !*end);
        assert(e.count>0 && expr_verify_lazy(e.u.tokens,e.t.types,e.count));
        player.var[0]=cases[j].x;player.var[1]=player_warnings=random_reads=0;result=0;f=0;
        type=Expr_EvalCtx(&player,&e,&result,&f,&frame);
        if(type!=cases[j].type || (type==2 ? f!=cases[j].value : result!=(int)cases[j].value) ||
            player.var[1]!=cases[j].side || player_warnings!=cases[j].warnings || random_reads!=cases[j].random)
            fprintf(stderr,"lazy case %zu: type=%d int=%d float=%g side=%d warnings=%d random=%d\n",j,type,result,(double)f,player.var[1],player_warnings,random_reads);
        assert(type==cases[j].type && (type==2 ? f==cases[j].value : result==(int)cases[j].value));
        assert(player.var[1]==cases[j].side && player_warnings==cases[j].warnings && random_reads==cases[j].random && g_exprStackTop==before);
        Expr_Free(&e);zss_no_allocs();
    }
    /* Legacy CNS remains eager and boolean-valued. */
    assert(Expr_Compile("0 && (var(1):=9)",&e,&owner,&end,EXPR_WANT_ANY));player.var[1]=0;
    assert(Expr_Eval(&player,&e,&result,&f)==1 && result==0 && player.var[1]==9);Expr_Free(&e);zss_no_allocs();
    /* NaN and bottom have false logical truth, while the selected operand's
     * numeric representation is retained on a non-folded path. */
    values[0].f=NAN;types[0]=EXPR_FLOAT;
    assert(Expr_CompileCtx("$x || 7",&e,&owner,&end,EXPR_WANT_ANY,&compile));
    assert(Expr_EvalCtx(&player,&e,&result,&f,&frame)==1 && result==7);Expr_Free(&e);
    assert(Expr_CompileCtx("cond($x && 1,2,3)",&e,&owner,&end,EXPR_WANT_ANY,&compile));
    assert(Expr_EvalCtx(&player,&e,&result,&f,&frame)==1 && result==3);Expr_Free(&e);zss_no_allocs();types[0]=EXPR_INT;
    /* Cloning nested cond payloads must retain their own verified jump code. */
    values[0].i=0;values[1].i=4;
    assert(Expr_CompileCtx(owned,&e,&owner,&end,EXPR_WANT_ANY,&compile));assert(Expr_Clone(&copy,&e));Expr_Free(&e);
    assert(Expr_EvalCtx(&player,&copy,&result,&f,&frame)==1 && result==3);Expr_Free(&copy);zss_no_allocs();
    /* Every incoming edge must agree on stack depth, and no jump may enter an
     * interval comparison tag, leave the code, or point backward/to itself. */
    assert(Expr_CompileCtx("var(0) || (var(1)=[0,2])",&e,&owner,&end,EXPR_WANT_ANY,&compile));
    for(at=0;at<e.count && e.t.types[at]!=EXPR_SC_OR;at++);assert(at<e.count);save=e.u.tokens[at].i;
    e.u.tokens[at].i=0;assert(!expr_verify_lazy(e.u.tokens,e.t.types,e.count));
    assert(!Expr_EvalCtx(&player,&e,&result,&f,&frame) && g_exprStackTop==before);
    e.u.tokens[at].i=INT_MAX;assert(!expr_verify_lazy(e.u.tokens,e.t.types,e.count));
    assert(!Expr_EvalCtx(&player,&e,&result,&f,&frame) && g_exprStackTop==before);
    e.u.tokens[at].i=1;assert(!expr_verify_lazy(e.u.tokens,e.t.types,e.count));
    for(j=(size_t)at+1;j<(size_t)e.count;j++) if(e.t.types[j]==EXPR_OP && e.u.tokens[j].i==EXPR_OP_IN_CC) break;
    assert(j+1<(size_t)e.count);e.u.tokens[at].i=(int)j+1-at;assert(!expr_verify_lazy(e.u.tokens,e.t.types,e.count));
    e.u.tokens[at].i=save;assert(expr_verify_lazy(e.u.tokens,e.t.types,e.count));
    assert(!Expr_Eval(&player,&e,&result,&f) && g_exprStackTop==before);Expr_Free(&e);zss_no_allocs();
    used=0;for(j=0;j<60;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,j ? "||1" : "1");
    assert(Expr_CompileCtx(text,&e,&owner,&end,EXPR_WANT_ANY,&compile));
    assert(e.count>EXPR_MAX_TOKENS && Expr_EvalCtx(&player,&e,&result,&f,&frame)==1 && result==1);Expr_Free(&e);zss_no_allocs();
    for(j=60;j<70;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"||1");
    assert(!Expr_CompileCtx(text,&e,&owner,&end,EXPR_WANT_ANY,&compile) && !compile.depth);zss_no_allocs();
    assert(!Expr_CompileCtx("~1 || 0",&e,&owner,&end,EXPR_WANT_ANY,&compile));zss_no_allocs();
    assert(!Expr_CompileCtx("~1",&e,&owner,&end,EXPR_WANT_ANY,&compile));zss_no_allocs();
    /* Plain malloc buffers let the real reallocating legacy scaling helpers
     * run without the harness's calloc-only allocation bookkeeping. All copied
     * operands here are scalar; no trigger ownership is duplicated. */
    assert(Expr_CompileCtx("$x || $y",&e,&owner,&end,EXPR_WANT_ANY,&compile));
    copy.count=e.count;copy.u.tokens=malloc((size_t)e.count*sizeof(ExprValue));copy.t.types=malloc((size_t)e.count);
    assert(copy.u.tokens && copy.t.types);
    memcpy(copy.u.tokens,e.u.tokens,(size_t)e.count*sizeof(ExprValue));memcpy(copy.t.types,e.t.types,(size_t)e.count);Expr_Free(&e);
    values[0].i=2;values[1].i=0;
    Expr_MulFloat(&copy,2);assert(expr_verify_lazy(copy.u.tokens,copy.t.types,copy.count));
    assert(Expr_EvalCtx(&player,&copy,&result,&f,&frame)==2 && f==4);
    Expr_Halve(&copy);assert(expr_verify_lazy(copy.u.tokens,copy.t.types,copy.count));
    assert(Expr_EvalCtx(&player,&copy,&result,&f,&frame)==2 && f==2);Expr_Free(&copy);zss_no_allocs();
    zss_calloc_calls=0;assert(Expr_CompileCtx(owned,&e,&owner,&end,EXPR_WANT_ANY,&compile));calls=zss_calloc_calls;Expr_Free(&e);
    for(j=1;j<=(size_t)calls;j++) {
        zss_calloc_calls=0;zss_fail_call=(int)j;
        if(Expr_CompileCtx(owned,&e,&owner,&end,EXPR_WANT_ANY,&compile)) Expr_Free(&e);
        assert(!e.count && !compile.depth);zss_no_allocs();
    }
    zss_fail_call=0;
    puts("ok: ZSS short circuit, value/type selection, forward-jump verification and nested ownership");
}
static int reenter_zss(Player *p, StateCtrl *sc, ExprEvalCtx *frame, void *context)
{
    ZssProgram *program=context;
    int result, before;
    assert(sc->type==SCTRL_CTRLSET);
    before=Expr_EvalIntCtx(p,&sc->arg[0],0,frame);
    assert(Zss_RunBody(program,1,p,reenter_zss,context,&result) && !result);
    assert(Expr_EvalIntCtx(p,&sc->arg[0],0,frame)==before);
    return 0;
}
static void test_zss_controller_contexts(void)
{
    const struct {const char *source;int type,arg;} cases[]={
        {"VelSet{x:$x;y:$y}",SCTRL_VELSET,3},
        {"VelAdd{x:cond($y,$x,0)}",SCTRL_VELADD,3},
        {"PosSet{x:$x;y:$y}",SCTRL_POSSET,3},
        {"PosAdd{x:$x}",SCTRL_POSADD,3},
        {"CtrlSet{value:$x}",SCTRL_CTRLSET,0},
        {"ChangeState{value:$x;ctrl:$y;anim:$x}",SCTRL_CHANGESTATE,0},
        {"ChangeAnim{value:$x;elem:$y}",SCTRL_CHANGEANIM,0},
        {"Turn{}",SCTRL_TURN,-1}, {"Null{}",SCTRL_NULL,-1},
        {"ClearClipboard{}",SCTRL_CLEARCLIPBOARD,-1}
    };
    const char *text="DisplayToClipboard{text:\"$x=%i y=%g k=%i\";params:$x,$y,cond($x=4,8,9)}";
    const char *bad[]={"CtrlSet{value:$unknown}","CtrlSet{value:cond($x,1,$unknown)}","CtrlSet{value:$x,1}","CtrlSet{value:$x &&}","CtrlSet{}","CtrlSet{value:$x;value:1}","Helper{id:$x}","ChangeState{ctrl:$x}","DisplayToClipboard{text:\"%i %i\";params:$x}","VelSet{x:$x,1}"};
    const char *body="[StateDef 0]\nlet value=1;\nCtrlSet{value:$value}\nlet value=$value+1;\nCtrlSet{value:$value}\nfor i=1;3 {CtrlSet{value:$value+$i}}\n";
    StateCtrl sc={0}, failed={0};
    ZssProgram *program=NULL;
    ZssDiagnostic d;
    ExprCompileCtx compile={&owner,context_slot,0};
    ExprValue values[2]={{0},{0}};
    char types[2]={EXPR_INT,EXPR_FLOAT};
    ExprEvalCtx frame={values,types,2};
    size_t j;
    int result, calls;
    values[0].i=3;values[1].f=1.5f;
    for(j=0;j<sizeof(cases)/sizeof(*cases);j++) {
        assert(Zss_CompileControllerCtx(cases[j].source,strlen(cases[j].source),&owner,&sc,&d,&compile));
        assert(sc.type==cases[j].type);
        if(cases[j].arg>=0) assert(Expr_EvalFloatCtx(&player,&sc.arg[cases[j].arg],&frame)==3.0f);
        StateCtrl_Free(&sc);zss_no_allocs();
    }
    assert(Zss_CompileControllerCtx(text,strlen(text),&owner,&sc,&d,&compile));
    run_clipboard(&player,&sc,&frame);assert(!strcmp(clipboard,"$x=3 y=1.5 k=9"));
    values[0].i=4;values[1].f=2.5f;
    run_clipboard(&player,&sc,&frame);assert(!strcmp(clipboard,"$x=4 y=2.5 k=8"));
    sc.type=SCTRL_APPENDTOCLIPBOARD;run_clipboard(&player,&sc,&frame);
    assert(!strcmp(clipboard,"$x=4 y=2.5 k=8$x=4 y=2.5 k=8"));StateCtrl_Free(&sc);zss_no_allocs();
    assert(!Zss_CompileController("CtrlSet{value:$x}",strlen("CtrlSet{value:$x}"),&owner,&failed,&d));zss_no_allocs();
    for(j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        assert(!Zss_CompileControllerCtx(bad[j],strlen(bad[j]),&owner,&failed,&d,&compile));
        assert(!failed.params && !failed.arg[0].count && d.message && !compile.depth);zss_no_allocs();
    }
    assert(Zss_CompileProgram(body,strlen(body),&owner,&program,&d));visits=stop_visit=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && visits==5 && visited[0]==1 && visited[1]==2 && visited[2]==3 && visited[3]==4 && visited[4]==5);
    Zss_FreeProgram(program);zss_no_allocs();
    /* Inject failure into every owned allocation while compiling local/nested
     * clipboard parameters, including its text payload and temporary INI. */
    zss_calloc_calls=0;assert(Zss_CompileControllerCtx(text,strlen(text),&owner,&sc,&d,&compile));calls=zss_calloc_calls;StateCtrl_Free(&sc);
    for(j=1;j<=(size_t)calls;j++) {
        zss_calloc_calls=0;zss_fail_call=(int)j;
        if(Zss_CompileControllerCtx(text,strlen(text),&owner,&failed,&d,&compile)) StateCtrl_Free(&failed);
        assert(!failed.params && !failed.arg[0].count && !compile.depth);zss_no_allocs();
    }
    zss_fail_call=0;
    puts("ok: local controller parameters, runtime frame binding, clipboard formatting and partial cleanup");
}
static void test_zss_locals(void)
{
    const char *source="[StateDef 0]\nlet a=1;\nlet b=2.5;\nlet a=$a+3;\nvar(0):=$a;\nfvar(0):=$b;\nvar(1):=0;\nfor i=0;3 {var(1):=var(1)+$i;}\nvar(2):=$i;\n";
    const char *mutate="[StateDef 0]\nvar(0):=0;\nfor i=0;4 {var(0):=var(0)+1;let i=$i+1;}\nvar(1):=$i;\n";
    const char *lifetime="[StateDef 0]\nvar(0):=0;\nif var(2)=0 {let x=9;}\nif $x {var(0):=9;}\n";
    const char *nested="[StateDef 0]\nvar(0):=0;\nfor i=1;3 {for j=1;3 {if $j=2 {continue;} if $i=3 {break;} var(0):=var(0)+$i*$j;}}\n";
    const char *bad[]={"let a=;", "let a=1", "let a,b=1,2;", "let $a=1;", "let a=1;\n$a:=2;", "var(0):=$unknown;", "let a=$unknown;", "let a=1;\nCtrlSet{value:$unknown}", "let a=1;\n[StateDef 1]\nvar(0):=$a;", "for i=0;3; {}"};
    ZssProgram *program=NULL, *failed=NULL;
    ZssDiagnostic d;
    char text[16384];
    size_t j, used;
    int result, calls;
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && player.var[0]==4 && player.fvar[0]==2.5f && player.var[1]==6 && player.var[2]==3);
    Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(mutate,strlen(mutate),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && player.var[0]==3 && player.var[1]==5);Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(lifetime,strlen(lifetime),&owner,&program,&d));player.var[2]=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));assert(player.var[0]==9);
    player.var[2]=1;assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));assert(!player.var[0]);
    Zss_FreeProgram(program);zss_no_allocs();
    assert(Zss_CompileProgram(nested,strlen(nested),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(player.var[0]==12);Zss_FreeProgram(program);zss_no_allocs();
    strcpy(text,"[StateDef 0]\nfor i=2;$i+2 {let _=var(0):=var(0)+1;}\nvar(1):=$i;\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));player.var[0]=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(player.var[0]==3 && player.var[1]==4);Zss_FreeProgram(program);zss_no_allocs();
    strcpy(text,"[StateDef 0]\nlet A=0;\nvar($a):=7;\nvar(1):=var($A);\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(player.var[0]==7 && player.var[1]==7);Zss_FreeProgram(program);zss_no_allocs();
    /* Every slot, including 255, is scalar and bounded on either pointer width. */
    used=(size_t)snprintf(text,sizeof(text),"[StateDef 0]\n");
    for(j=0;j<256;j++) used+=(size_t)snprintf(text+used,sizeof(text)-used,"let x%zu=%zu;\n",j,j);
    strcpy(text+used,"var(0):=$x255;\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));assert(player.var[0]==255);
    Zss_FreeProgram(program);zss_no_allocs();strcpy(text+used,"let x256=0;\n");
    assert(!Zss_CompileProgram(text,strlen(text),&owner,&failed,&d));assert(!failed && d.message);zss_no_allocs();
    for(j=0;j<sizeof(bad)/sizeof(*bad);j++) {
        snprintf(text,sizeof(text),"[StateDef 0]\n%s",bad[j]);
        assert(!Zss_CompileProgram(text,strlen(text),&owner,&failed,&d));assert(!failed && d.message);zss_no_allocs();
    }
    strcpy(text,"[StateDef 0]\nlet x=3;\nCtrlSet{value:$x}\nvar(0):=$x;\n[StateDef 1]\nlet x=9;\nvar(1):=$x;\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,reenter_zss,program,&result));
    assert(!result && player.var[0]==3 && player.var[1]==9);Zss_FreeProgram(program);zss_no_allocs();
    strcpy(text,"[StateDef 0]\nfvar(0):=0;\nfor i=1.5;3 {fvar(0):=fvar(0)+$i;}\nvar(0):=$i;\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && player.fvar[0]==6.5f && player.var[0]==3);Zss_FreeProgram(program);zss_no_allocs();
    strcpy(text,"[StateDef 0]\nvar(0):=0;\nfor i=0;2500 {var(0):=var(0)+1;}\nvar(1):=$i;\n");
    assert(Zss_CompileProgram(text,strlen(text),&owner,&program,&d));player_warnings=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && player.var[0]==2500 && player.var[1]==2500 && player_warnings==1);Zss_FreeProgram(program);zss_no_allocs();
    zss_calloc_calls=0;assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));calls=zss_calloc_calls;Zss_FreeProgram(program);
    for(j=1;j<=(size_t)calls;j++) {
        zss_calloc_calls=0;zss_fail_call=(int)j;
        if(Zss_CompileProgram(source,strlen(source),&owner,&failed,&d)){Zss_FreeProgram(failed);failed=NULL;}
        assert(!failed);zss_no_allocs();
    }
    zss_fail_call=0;
    puts("ok: numeric locals, named for counters, lexical slots, fresh frames and allocation rollback");
}
static void test_zss_program(void)
{
    const char *source="[StateDef 200;type:S;]\nif var(0)=0 { CtrlSet{value:1} CtrlSet{value:2} } else if var(0)=9 { CtrlSet{value:9} } else { CtrlSet{value:8} }\nif .5 { CtrlSet{value:3} }\n[StateDef 201]\n";
    const char *bad[]={"", "[StateDef 0] Null{}", "[StateDef\n0] Null{}", "[StateDef 0]\n[StateDef 0]", "[StateDef 0]\nif 1 {", "[StateDef 0]\nif 0 && {Null{}}", "[StateDef 0;ctrl:1&&0]", "[StateDef 0]\nNull{};", "[StateDef 0]\nelse {Null{}}", "[StateDef 0]\nNull{} Null{}"};
    ZssProgram *program=NULL, *failed=NULL;
    ZssDiagnostic d;
    const StateDef *sd;
    int result, number;
    size_t i;
    int calls;
    char *owned_source=malloc(strlen(source)+1);
    assert(owned_source);strcpy(owned_source,source);
    assert(Zss_CompileProgram(owned_source,strlen(owned_source),&owner,&program,&d));
    memset(owned_source,0,strlen(source));free(owned_source);
    assert(Zss_ProgramStateCount(program)==2);
    sd=Zss_ProgramState(program,0,&number);assert(sd && number==200 && sd->type==STATETYPE_STAND);
    assert(!Zss_ProgramState(program,2,NULL));
    player.var[0]=0;visits=stop_visit=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(!result && visits==3 && visited[0]==1 && visited[1]==2 && visited[2]==3);
    player.var[0]=9;visits=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(visits==2 && visited[0]==9 && visited[1]==3);
    player.var[0]=7;visits=0;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(visits==2 && visited[0]==8);
    player.var[0]=0;visits=0;stop_visit=1;
    assert(Zss_RunBody(program,0,&player,record_zss_controller,&owner,&result));
    assert(visits==1 && (result&STATECTRL_CHANGED) && (result&STATECTRL_STOP));
    visits=0;assert(Zss_RunBody(program,1,&player,record_zss_controller,&owner,&result) && !result && !visits);
    {
        ZssProgram *saved=program;
        assert(!Zss_CompileProgram("[bad]",5,&owner,&program,&d));
        assert(program==saved);
    }
    Zss_FreeProgram(program);zss_no_allocs();
    for(i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        assert(!Zss_CompileProgram(bad[i],strlen(bad[i]),&owner,&failed,&d));
        assert(!failed && d.message);zss_no_allocs();
    }
    zss_calloc_calls=0;
    assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    calls=zss_calloc_calls;Zss_FreeProgram(program);zss_no_allocs();
    for(i=1;i<=(size_t)calls;i++) {
        zss_calloc_calls=0;zss_fail_call=(int)i;
        /* Include constructor, condition and arena-growth allocation failures. */
        if(Zss_CompileProgram(source,strlen(source),&owner,&failed,&d)) {Zss_FreeProgram(failed);failed=NULL;}
        assert(!failed);zss_no_allocs();
    }
    zss_fail_call=0;
    {
        char many[2048], *w=many;
        for(i=0;i<10;i++)w+=sprintf(w,"[StateDef %u]\nNull{}\n",(unsigned)i);
        assert(Zss_CompileProgram(many,strlen(many),&owner,&program,&d));
        assert(Zss_ProgramStateCount(program)==10);Zss_FreeProgram(program);zss_no_allocs();
        w=many;w+=sprintf(w,"[StateDef 0]\n");
        for(i=0;i<33;i++)w+=sprintf(w,"if 1 {");
        for(i=0;i<33;i++)*w++='}';*w=0;
        assert(!Zss_CompileProgram(many,strlen(many),&owner,&failed,&d));zss_no_allocs();
    }
    alloc_calls=0;assert(Zss_CompileProgram(source,strlen(source),&owner,&program,&d));
    calls=alloc_calls;Zss_FreeProgram(program);
    for(i=1;i<=(size_t)calls;i++) {
        alloc_calls=0;fail_alloc=(int)i;
        if(Zss_CompileProgram(source,strlen(source),&owner,&failed,&d)){Zss_FreeProgram(failed);failed=NULL;}
        assert(!failed);zss_no_allocs();
    }
    fail_alloc=0;puts("ok: owned ZSS states, nested branches, one-time guards and stop propagation");
}
static void test_zss_state_header(void)
{
    StateDef sd={0}, empty={0};
    ZssDiagnostic d;
    int n=77;
    size_t i;
    const char *good="[StateDef 200; type:S; movetype:A; physics:N; velset:2+3,-0.5; ctrl:1; juggle:var(1)+2;]";
    const char *bad[]={"[StateDef 2147483648]","[StateDef -2147483649]","[StateDef 1.5]",
        "[StateDef +1]","[StateDef 2 type:S]","[StateDef 2; type:Stand]",
        "[StateDef 2; type:S; TYPE:A]","[StateDef 2; unknown:3]",
        "[StateDef 2; ctrl:$i]","[StateDef 2; ctrl:2+3; anim:NoSuchTrigger]",
        "[StateDef 2; ctrl:(1+2]","[StateDef 2] Null{}"};
    assert(Zss_CompileStateHeader("[StateDef 0]",12,&owner,&n,&sd,&d));
    assert(n==0 && sd.type==STATETYPE_STAND && sd.movetype==MOVETYPE_IDLE);
    assert(Expr_EvalInt(&player,&sd.ctrl,0)==-1 && Expr_EvalFloat(&player,&sd.velset_x)==-FLT_MAX);
    StateDef_FreeProperties(&sd);memset(&sd,0,sizeof(sd));
    player.var[1]=7;
    assert(Zss_CompileStateHeader(good,strlen(good),&owner,&n,&sd,&d));
    assert(n==200 && sd.movetype==MOVETYPE_ATTACK && sd.ctrl.u.value.i==1);
    assert(Expr_EvalFloat(&player,&sd.velset_x)==5 && Expr_EvalFloat(&player,&sd.velset_y)==-0.5f);
    assert(Expr_EvalInt(&player,&sd.juggle,0)==9);StateDef_FreeProperties(&sd);memset(&sd,0,sizeof(sd));
    assert(Zss_CompileStateHeader("[StateDef -2147483648]",strlen("[StateDef -2147483648]"),&owner,&n,&sd,&d));
    assert(n==INT_MIN && !sd.juggle.count);StateDef_FreeProperties(&sd);memset(&sd,0,sizeof(sd));
    {
        const char *interval="[StateDef 1; juggle:var(1)=[1,9];]";
        assert(Zss_CompileStateHeader(interval,strlen(interval),&owner,&n,&sd,&d));
        assert(Expr_EvalInt(&player,&sd.juggle,0)==1);StateDef_FreeProperties(&sd);memset(&sd,0,sizeof(sd));
    }
    for(i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        n=77;assert(!Zss_CompileStateHeader(bad[i],strlen(bad[i]),&owner,&n,&sd,&d));
        assert(n==77 && !memcmp(&sd,&empty,sizeof(sd)) && d.message);zss_no_allocs();
    }
    for(i=1;i<=8;i++) {
        zss_calloc_calls=0;zss_fail_call=(int)i;
        if(Zss_CompileStateHeader(good,strlen(good),&owner,&n,&sd,&d))StateDef_FreeProperties(&sd);
        memset(&sd,0,sizeof(sd));zss_no_allocs();
    }
    zss_fail_call=0;
    for(i=1;i<=6;i++) {
        alloc_calls=0;fail_alloc=(int)i;
        if(Zss_CompileStateHeader(good,strlen(good),&owner,&n,&sd,&d))StateDef_FreeProperties(&sd);
        memset(&sd,0,sizeof(sd));zss_no_allocs();
    }
    fail_alloc=0;puts("ok: ZSS StateDef headers share CNS properties, defaults and partial cleanup");
}
static void test_zss_controller(void)
{
    StateCtrl sc={0}, empty={0};
    ZssDiagnostic diagnostic;
    ScriptParam pairs[]={{"type","Null",1},{"text","\"MiX;#[]{}\"",7}};
    const char *error;
    Ini *ini=ScriptParams_ToIni(pairs,2,&error);
    const char *valid="VelSet { x: 2 + # comment\n 3; y: -0.5; }";
    const char *text="DisplayToClipboard { text: \"MiX;#[]{}\"; }";
    const char *bad[]={"VelSet{x:1; X:2}","VelSet{x:1};","VelSet{x:$i}","VelSet{x:}","VelSet{x:(1+2}","VelSet{x:1e}","NoSuchController{}","VelSet{type:Null}","VelSet{x:1} Null{}","VelSet{unknown:1}","HitDef{}","Null{x:1}","ChangeState{}","ChangeState{value:2+3;ctrl:NoSuchTrigger}","DisplayToClipboard{text:\"%d\";params:1,2}"};
    size_t i;
    assert(ini && !error);
    assert(!strcmp(Ini_GetValue(ini,"text"),pairs[1].value));
    assert(Ini_SourceLine(Ini_FindKeyLine(ini,"text"),ini)==6);
    assert(Ini_CountGroups(ini,"zss")==1);Ini_Rewind(ini);
    assert(!strcmp(Ini_GetValue(ini,"type"),"Null"));Ini_Free(ini);
    assert(Zss_CompileController(valid,strlen(valid),&owner,&sc,&diagnostic));
    assert(sc.type==SCTRL_VELSET && Expr_EvalFloat(&player,&sc.arg[3])==5.0f);
    assert(Expr_EvalFloat(&player,&sc.arg[4])==-0.5f);StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));
    player.var[1]=7;
    { const char *v="VelSet{x:var(1)+3}";
      assert(Zss_CompileController(v,strlen(v),&owner,&sc,&diagnostic));
      assert(Expr_EvalFloat(&player,&sc.arg[3])==10);StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc)); }
    assert(Zss_CompileController(text,strlen(text),&owner,&sc,&diagnostic));
    assert(!strcmp(((CnsText *)sc.params)->text,"MiX;#[]{}"));StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));
    for(i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        assert(!Zss_CompileController(bad[i],strlen(bad[i]),&owner,&sc,&diagnostic));
        assert(diagnostic.message && diagnostic.line && diagnostic.column);
        assert(!memcmp(&sc,&empty,sizeof(sc)));check_no_allocs();
    }
    g_iniCaseSensitive=1;
    assert(Zss_CompileController("VelSet{X:3}",11,&owner,&sc,&diagnostic));
    assert(Expr_EvalFloat(&player,&sc.arg[3])==3);StateCtrl_Free(&sc);memset(&sc,0,sizeof(sc));
    g_iniCaseSensitive=0;
    zss_no_allocs();
    for(i=1;i<=8;i++) {
        zss_calloc_calls=0;zss_fail_call=(int)i;
        if(Zss_CompileController(valid,strlen(valid),&owner,&sc,&diagnostic))StateCtrl_Free(&sc);
        else assert(diagnostic.message);
        memset(&sc,0,sizeof(sc));zss_no_allocs();
    }
    zss_fail_call=0;
    for(i=1;i<=2;i++) {
        alloc_calls=0;fail_alloc=(int)i;
        assert(!Zss_CompileController(valid,strlen(valid),&owner,&sc,&diagnostic));
        assert(!memcmp(&sc,&empty,sizeof(sc)));zss_no_allocs();
    }
    fail_alloc=0;
    pairs[1].key="TYPE";assert(!ScriptParams_ToIni(pairs,2,&error) && error);
    pairs[1].key="bad=key";assert(!ScriptParams_ToIni(pairs,2,&error));
    check_no_allocs();puts("ok: ZSS controller parsing uses real INI and MUGEN parameter/expression compilers");
}
#endif
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    TEST_REPORT_ABI();
    g_game = &game; owner.mugenversion = 0x01010000;
    g_exprStackTop = -1;
#ifdef TEST_ZSS_CONTROLLER
    test_zss_controller();
    test_zss_state_header();
    test_zss_program();
    test_zss_loops();
    test_zss_ranges();
    test_expression_contexts();
    test_zss_lazy();
    test_zss_locals();
    test_zss_controller_contexts();
    test_zss_installation();
#else
    test_controller_parse(); test_variables(); test_clipboard();
    test_expr_allocs(); test_trigger_copy_math(); test_state_headers();
#endif
    assert(g_exprStackTop == -1); check_no_allocs();
#ifdef TEST_ZSS_CONTROLLER
    puts("ok: ZSS controller bridge regressions passed");
#else
    puts("ok: CNS ABI regressions passed");
#endif
    return 0;
}
