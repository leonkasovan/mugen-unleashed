/* Port addition: shared token-to-parameter reader for ZSS headers and leaves. */
#include "mugen.h"
#include "zss_params.h"
int Zss_ReadParams(ZssLexer *lex, int close, const char *type,
                    ZssParamBlock *block, ZssDiagnostic *diagnostic)
{
    ScriptParam *pairs;
    char *storage = NULL, *write, *start;
    size_t count = 0, n;
    int kind, tokens, parens, brackets;
    const char *message = "Invalid parameter block";
    if (block == NULL || lex == NULL) return 0;
    pairs = block->pairs;
    memset(block, 0, sizeof(*block));
    if (lex->size > ZSS_MAX_SOURCE || (close != '}' && close != ']') ||
        (type != NULL && strlen(type) > lex->size)) goto done;
    storage = calloc(1, lex->size * 2 + 16);
    if (storage == NULL) { message = "Out of memory parsing parameters"; goto done; }
    write = storage;
    if (type != NULL) {
        pairs[count].key = "type"; pairs[count].value = write;
        pairs[count++].line = (int)lex->token.line;
        n = strlen(type); memcpy(write, type, n + 1); write += n + 1;
    }
    kind = lex->token.kind == close ? close : ZssLex_Next(lex);
    while (kind != close) {
        message = "Expected parameter name or closing delimiter";
        if (kind != ZSS_IDENT) goto done;
        if (count >= ZSS_MAX_PARAMS + (type != NULL ? 1u : 0u)) { message = "Too many script parameters"; goto done; }
        pairs[count].key = write; pairs[count].line = (int)lex->token.line;
        memcpy(write, lex->source + lex->token.offset, lex->token.length); write += lex->token.length; *write++ = 0;
        message = "Expected ':' after parameter name";
        if (ZssLex_Next(lex) != ':') goto done;
        start = write; tokens = parens = brackets = 0;
        kind = ZssLex_Next(lex);
        while (kind != ';' && !(kind == close && parens == 0 && brackets == 0)) {
            message = "Incomplete parameter value";
            if (kind <= ZSS_EOF || kind == '{' || kind == '}') goto done;
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
            n = lex->token.length;
            memcpy(write, lex->source + lex->token.offset, n); write += n;
            kind = ZssLex_Next(lex);
        }
        if (tokens == 0 || parens != 0 || brackets != 0) { message = "Empty or unclosed parameter value"; goto done; }
        *write++ = 0; pairs[count++].value = start;
        if (kind == ';') kind = ZssLex_Next(lex);
    }

    block->count = count; block->storage = storage;
    return 1;
done:
    free(storage);
    if (diagnostic != NULL) {
        diagnostic->message = lex->error != NULL ? lex->error : message;
        diagnostic->line = lex->token.line; diagnostic->column = lex->token.column;
    }
    return 0;
}
