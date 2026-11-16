/* Port addition: ZSS state headers use the CNS property compiler. File bodies,
 * state installation, locals and expression contexts are separate milestones. */
#include "mugen.h"
#include "zss_params.h"
#include "zss_statedef.h"

static int state_number(ZssLexer *lex, int negative, int *out)
{
    size_t i;
    uint32_t n = 0, limit = negative ? 2147483648u : 2147483647u;
    if (lex->token.kind != ZSS_INT) return 0;
    for (i = 0; i < lex->token.length; i++) {
        unsigned digit = (unsigned)(lex->source[lex->token.offset + i] - '0');
        if (n > (limit - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    *out = negative ? (n == 2147483648u ? (-2147483647 - 1) : -(int)n) : (int)n;
    return 1;
}
static int valid_property(const ScriptParam *p)
{
    static const char *const names[] = {"juggle","facep2","hitdefpersist",
        "hitcountpersist","movehitpersist","sprpriority","velset","ctrl","anim","poweradd"};
    size_t i;
    const char *letters = NULL;
    if (!_stricmp(p->key,"type")) letters = "ACLSUaclsu";
    if (!_stricmp(p->key,"movetype")) letters = "AHIUahiu";
    if (!_stricmp(p->key,"physics")) letters = "ACNSUacnsu";
    if (letters != NULL) return strlen(p->value) == 1 && strchr(letters,p->value[0]) != NULL;
    for (i = 0; i < sizeof(names)/sizeof(*names); i++) if (!_stricmp(p->key,names[i])) return 1;
    return 0;
}
int Zss_CompileStateHeader(const char *source, size_t size, CharData *owner,
                           int *number, StateDef *out, ZssDiagnostic *diagnostic)
{
    ZssLexer lex;
    ZssParamBlock block = {0};
    StateDef state = {0};
    Ini *ini = NULL;
    const char *message = "Expected '[StateDef number; ...]'";
    size_t i;
    int n, negative = 0;
    memset(&lex,0,sizeof(lex));lex.token.line=lex.token.column=1;
    if (diagnostic != NULL) memset(diagnostic,0,sizeof(*diagnostic));
    if (source == NULL || owner == NULL || number == NULL || out == NULL || size > ZSS_MAX_SOURCE) goto failed;
    if (!ZssLex_Init(&lex,source,size,65536)) {message=lex.error;goto failed;}
    if (ZssLex_Next(&lex) != '[' || ZssLex_Next(&lex) != ZSS_IDENT || !ZssLex_Is(&lex,"statedef")) goto failed;
    if (ZssLex_Next(&lex) == '-') {negative=1;ZssLex_Next(&lex);}
    message="State number must be a signed 32-bit decimal integer";
    if (!state_number(&lex,negative,&n)) goto failed;
    message="Expected ';' or ']' after state number";
    if (ZssLex_Next(&lex) != ';' && lex.token.kind != ']') goto failed;
    if (!Zss_ReadParams(&lex,']',NULL,&block,diagnostic)) goto cleanup;
    message="Unexpected text after StateDef header";
    if (ZssLex_Next(&lex) != ZSS_EOF) goto failed;
    for (i=0;i<block.count;i++) {
        if (!valid_property(&block.pairs[i])) {
            message="Unknown StateDef property or invalid literal state type";goto failed;
        }
    }
    ini=ScriptParams_ToIni(block.pairs,block.count,&message);
    if (ini == NULL) goto failed;
    if (!StateDef_CompileProperties(n,&state,ini,owner)) {
        message="StateDef properties rejected by MUGEN compiler";goto failed;
    }
    *number=n;*out=state;
    Ini_Free(ini);free(block.storage);
    return 1;
failed:
    if (diagnostic != NULL) {
        diagnostic->message=lex.error != NULL ? lex.error : message;
        diagnostic->line=lex.token.line;diagnostic->column=lex.token.column;
    }
cleanup:
    StateDef_FreeProperties(&state);Ini_Free(ini);free(block.storage);
    return 0;
}
