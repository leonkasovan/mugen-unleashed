#ifndef MUGEN_ZSS_CONTROLLER_H
#define MUGEN_ZSS_CONTROLLER_H
/* Port addition: first compiler bridge. This compiles exactly one controller
 * block using MUGEN expressions for an explicitly enabled controller subset. It is not a ZSS state/file compiler yet. */
typedef struct ZssDiagnostic {
    const char *message;
    uint32_t line, column;
} ZssDiagnostic;
/* out must be empty. It is untouched on failure and owns the result on success;
 * StateCtrl_Free releases it. The legacy wrapper rejects local expressions. */
int Zss_CompileController(const char *source, size_t size, CharData *owner,
                          StateCtrl *out, ZssDiagnostic *diagnostic);
/* Explicit lexical context for the audited engine controller subset. */
int Zss_CompileControllerCtx(const char *source, size_t size, CharData *owner,
                             StateCtrl *out, ZssDiagnostic *diagnostic, ExprCompileCtx *ctx);
#endif
