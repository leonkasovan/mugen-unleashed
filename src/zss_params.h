#ifndef MUGEN_ZSS_PARAMS_H
#define MUGEN_ZSS_PARAMS_H
#include "scriptparams.h"
#include "zss_lex.h"
#include "zss_controller.h"
#define ZSS_MAX_PARAMS 128
#define ZSS_MAX_SOURCE (1024u * 1024u)
typedef struct ZssParamBlock {
    ScriptParam pairs[ZSS_MAX_PARAMS + 1];
    size_t count;
    char *storage;
} ZssParamBlock;
/* Lexer is at an opening delimiter or header ';'. Success leaves it at close.
 * The caller bounds source size and, when supplied, type comes from that source.
 * Caller frees block->storage; strings borrow that allocation. */
int Zss_ReadParams(ZssLexer *lex, int close, const char *type,
                    ZssParamBlock *block, ZssDiagnostic *diagnostic);
/* Local tokens require an explicit context; header/legacy callers reject them. */
int Zss_ReadParamsCtx(ZssLexer *lex, int close, const char *type,
                       ZssParamBlock *block, ZssDiagnostic *diagnostic, ExprCompileCtx *ctx);
#endif
