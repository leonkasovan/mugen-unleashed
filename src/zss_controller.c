/* Port addition: parse a standalone ZSS controller into the existing CNS
 * controller representation. Parameters retain string case and delimiters;
 * comments are removed by tokens, never by Ini_Parse. */
#include "mugen.h"
#include "scriptparams.h"
#include "zss_lex.h"
#include "zss_controller.h"
#include <limits.h>

#define ZSS_CONTROLLER_MAX_PARAMS 128
#define ZSS_CONTROLLER_MAX_SOURCE (1024u * 1024u)

/* Initial leaf schemas; block attributes belong to the future block parser. */
static int allowed_key(const char *type, const char *key)
{
    if (!_stricmp(type,"velset") || !_stricmp(type,"veladd") ||
        !_stricmp(type,"posset") || !_stricmp(type,"posadd"))
        return !_stricmp(key,"x") || !_stricmp(key,"y") || !_stricmp(key,"z");
    if (!_stricmp(type,"ctrlset")) return !_stricmp(key,"value");
    if (!_stricmp(type,"changestate"))
        return !_stricmp(key,"value") || !_stricmp(key,"ctrl") || !_stricmp(key,"anim");
    if (!_stricmp(type,"changeanim")) return !_stricmp(key,"value") || !_stricmp(key,"elem");
    if (!_stricmp(type,"displaytoclipboard") || !_stricmp(type,"appendtoclipboard"))
        return !_stricmp(key,"text") || !_stricmp(key,"params");
    return 0;
}

int Zss_CompileController(const char *source, size_t size, CharData *owner,
                          StateCtrl *out, ZssDiagnostic *diagnostic)
{
    ZssLexer lex;
    ScriptParam pairs[ZSS_CONTROLLER_MAX_PARAMS + 1];
    StateCtrl controller = {0};
    Ini *ini = NULL;
    char *storage = NULL, *write, *start;
    const char *message = "Expected a controller name";
    size_t count = 0, n, i;
    int kind, parens, brackets, tokens;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    memset(&lex, 0, sizeof(lex)); lex.token.line = lex.token.column = 1;
    if (source == NULL || owner == NULL || out == NULL || size > ZSS_CONTROLLER_MAX_SOURCE) {
        message = "Invalid or oversized controller source"; goto done;
    }
    if (!ZssLex_Init(&lex, source, size, 65536)) { message = lex.error; goto done; }
    if (ZssLex_Next(&lex) != ZSS_IDENT) goto lexical;
    /* Enable only payloads with audited partial-failure ownership in this
     * first bridge. Complex controllers are deferred to the shared-context
     * migration rather than exposing legacy partial-parse cleanup hazards. */
    if (!(ZssLex_Is(&lex, "velset") || ZssLex_Is(&lex, "veladd") ||
          ZssLex_Is(&lex, "posset") || ZssLex_Is(&lex, "posadd") ||
          ZssLex_Is(&lex, "ctrlset") || ZssLex_Is(&lex, "changestate") ||
          ZssLex_Is(&lex, "changeanim") || ZssLex_Is(&lex, "turn") ||
          ZssLex_Is(&lex, "null") || ZssLex_Is(&lex, "displaytoclipboard") ||
          ZssLex_Is(&lex, "appendtoclipboard") || ZssLex_Is(&lex, "clearclipboard"))) {
        message = "Controller not yet enabled in the ZSS bridge"; goto done;
    }
    /* One space per token fits in twice the source size; plus fixed key/NULs. */
    storage = calloc(1, size * 2 + 16);
    if (storage == NULL) { message = "Out of memory parsing controller"; goto done; }
    write = storage;
    pairs[count].key = "type"; pairs[count].value = write; pairs[count++].line = (int)lex.token.line;
    memcpy(write, source + lex.token.offset, lex.token.length); write += lex.token.length; *write++ = 0;
    message = "Expected '{' after controller name";
    if (ZssLex_Next(&lex) != '{') goto lexical;
    kind = ZssLex_Next(&lex);
    while (kind != '}') {
        message = "Expected parameter name or '}'";
        if (kind != ZSS_IDENT) goto lexical;
        if (count >= ZSS_CONTROLLER_MAX_PARAMS + 1) { message = "Too many controller parameters"; goto done; }
        pairs[count].key = write; pairs[count].line = (int)lex.token.line;
        memcpy(write, source + lex.token.offset, lex.token.length); write += lex.token.length; *write++ = 0;
        message = "Expected ':' after parameter name";
        if (ZssLex_Next(&lex) != ':') goto lexical;
        start = write; tokens = parens = brackets = 0;
        kind = ZssLex_Next(&lex);
        while (kind != ';' && kind != '}') {
            message = "Incomplete parameter value";
            if (kind <= ZSS_EOF || kind == '{') goto lexical;
            if (kind == ZSS_LOCAL) { message = "ZSS locals require the pending expression-context implementation"; goto done; }
            if (kind == '(') parens++;
            if (kind == '[') brackets++;
            /* MUGEN intervals also use mixed [a,b) / (a,b] closures. */
            if (kind == ')' || kind == ']') {
                if (kind == ')' && parens > 0) parens--;
                else if (brackets > 0) brackets--;
                else if (parens > 0) parens--;
                else { message = "Unmatched parameter delimiter"; goto done; }
            }
            if (tokens >= EXPR_OP_STACK_SIZE - 1) {
                message = "Parameter exceeds initial ZSS bridge token limit"; goto done;
            }
            if (tokens++) *write++ = ' ';
            n = lex.token.length;
            memcpy(write, source + lex.token.offset, n); write += n;
            kind = ZssLex_Next(&lex);
        }
        if (tokens == 0 || parens != 0 || brackets != 0) { message = "Empty or unclosed parameter value"; goto done; }
        *write++ = 0; pairs[count++].value = start;
        if (kind == ';') kind = ZssLex_Next(&lex);
    }
    message = "Unexpected text after controller (no trailing ';')";
    if (ZssLex_Next(&lex) != ZSS_EOF) goto lexical;
    for (i = 1; i < count; i++) {
        if (!allowed_key(pairs[0].value, pairs[i].key)) {
            message = "Unknown parameter for enabled ZSS controller"; goto done;
        }
    }
    ini = ScriptParams_ToIni(pairs, count, &message);
    if (ini == NULL) goto done;
    /* Parameter expressions use the existing MUGEN dialect in this increment.
     * Strict ZSS expression contexts remain pending. */
    if (!StateCtrl_ParseParams(ini, &controller, owner)) {
        message = "Controller parameters rejected by MUGEN compiler"; goto done;
    }
    *out = controller;
    Ini_Free(ini); free(storage);
    return 1;
lexical:
    if (lex.error != NULL) message = lex.error;
done:
    StateCtrl_Free(&controller);
    Ini_Free(ini); free(storage);
    if (diagnostic != NULL) {
        diagnostic->message = message;
        diagnostic->line = lex.token.line; diagnostic->column = lex.token.column;
    }
    return 0;
}
