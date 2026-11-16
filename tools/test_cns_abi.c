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
    assert(p);
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
void Player_Warn(Player *p, const char *msg) { (void)p; (void)msg; }
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

#define free audit_free
#include "../src/trigger.c"
#define Trigger_Eval test_trigger_eval
#include "../src/expr.c"
#undef free
#undef Trigger_Eval
#include "../src/sctrl.c"
#include "../src/statedef.c"

static void test_trigger_eval(Player *p, const Trigger *t)
{
    int idx;
    assert(t->id == TRIGGER_VAR && t->redirect == TRIGGER_REDIRECT_NONE);
    assert(Trigger_PopInt(p, &idx) != EXPR_BOTTOM);
    assert(idx >= 0 && idx < PLAYER_VAR_COUNT);
    ExprStack_Push(p->var[idx], EXPR_INT);
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
    run_clipboard(&player, &sc); assert(!strcmp(clipboard, "100% %n")); free(sc.params);
    sc.params = CnsText_Parse("\"value=%d / %.2f%%\""); assert(sc.params);
    Expr_SetInt(&sc.arg[0], 7); Expr_SetFloat(&sc.arg[1], 1.5f);
    run_clipboard(&player, &sc); assert(!strcmp(clipboard, "value=7 / 1.50%"));
    sc.type = SCTRL_APPENDTOCLIPBOARD;
    run_clipboard(&player, &sc); assert(!strcmp(clipboard, "value=7 / 1.50%value=7 / 1.50%"));
    free(sc.params);
    sc.type = SCTRL_DISPLAYTOCLIPBOARD;
    sc.params = CnsText_Parse("\"left\\q%d\""); assert(sc.params);
    run_clipboard(&player, &sc); assert(!strcmp(clipboard, "left7")); free(sc.params);
    sc.params = CnsText_Parse("\"%99999d\""); assert(sc.params);
    run_clipboard(&player, &sc); assert(strlen(clipboard) == 4095); free(sc.params);
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
