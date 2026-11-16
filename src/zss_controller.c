/* Port addition: parse a standalone ZSS controller into the existing CNS
 * controller representation. Parameters retain string case and delimiters;
 * comments are removed by tokens, never by Ini_Parse. */
#include "mugen.h"
#include "zss_params.h"
#include "zss_lex.h"
#include "zss_controller.h"
#include <limits.h>


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
    return Zss_CompileControllerCtx(source,size,owner,out,diagnostic,NULL);
}
int Zss_CompileControllerCtx(const char *source, size_t size, CharData *owner,
                             StateCtrl *out, ZssDiagnostic *diagnostic, ExprCompileCtx *ctx)
{
    ZssLexer lex;
    ZssParamBlock block = {0};
    ScriptParam *pairs = block.pairs;
    char name[64];
    StateCtrl controller = {0};
    Ini *ini = NULL;
    const char *message = "Expected a controller name";
    size_t i;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    memset(&lex, 0, sizeof(lex)); lex.token.line = lex.token.column = 1;
    if (source == NULL || owner == NULL || out == NULL || size > ZSS_MAX_SOURCE) {
        message = "Invalid or oversized controller source"; goto done;
    }
    if (!ZssLex_Init(&lex, source, size, 65536)) { message = lex.error; goto done; }
    if (ZssLex_Next(&lex) != ZSS_IDENT) goto lexical;
    /* The engine's capability table is shared with the runtime context guard. */
    if (lex.token.length >= sizeof(name)) {message="Controller name too long";goto done;}
    memcpy(name, source + lex.token.offset, lex.token.length);
    name[lex.token.length] = 0;
    if (StateCtrl_ScriptType(name)<0) {
        message = "Controller not yet enabled in the ZSS bridge"; goto done;
    }
    message = "Expected '{' after controller name";
    if (ZssLex_Next(&lex) != '{') goto lexical;
    if (!Zss_ReadParamsCtx(&lex, '}', name, &block, diagnostic, ctx)) goto cleanup;
    message = "Unexpected text after controller (no trailing ';')";
    if (ZssLex_Next(&lex) != ZSS_EOF) goto lexical;
    for (i = 1; i < block.count; i++) {
        if (!allowed_key(pairs[0].value, pairs[i].key)) {
            message = "Unknown parameter for enabled ZSS controller"; goto done;
        }
    }
    ini = ScriptParams_ToIni(pairs, block.count, &message);
    if (ini == NULL) goto done;
    /* Locals remain symbolic operands; never snapshot parameter values here. */
    if (!StateCtrl_ParseParamsCtx(ini, &controller, owner, ctx)) {
        message = "Controller parameters rejected by MUGEN compiler"; goto done;
    }
    *out = controller;
    Ini_Free(ini); free(block.storage);
    return 1;
lexical:
    if (lex.error != NULL) message = lex.error;
done:
    if (diagnostic != NULL) {
        diagnostic->message = message;
        diagnostic->line = lex.token.line; diagnostic->column = lex.token.column;
    }
cleanup:
    StateCtrl_Free(&controller);
    Ini_Free(ini); free(block.storage);
    return 0;
}
