#ifndef MUGEN_ZSS_LEX_H
#define MUGEN_ZSS_LEX_H
#include <stddef.h>
#include <stdint.h>

/* Port addition: allocation-free ZSS tokens. Punctuation uses its ASCII code.
 * Spans borrow the source; the parser must own it until lowering completes. */
enum {
    ZSS_ERROR = -1, ZSS_EOF = 0, ZSS_IDENT = 256, ZSS_LOCAL,
    ZSS_INT, ZSS_FLOAT, ZSS_STRING, ZSS_ASSIGN, ZSS_NE, ZSS_GE,
    ZSS_LE, ZSS_AND, ZSS_OR, ZSS_XOR, ZSS_POWER
};
typedef struct ZssToken {
    int kind;
    size_t offset, length;
    uint32_t line, column;
} ZssToken;
typedef struct ZssLexer {
    const char *source;
    size_t size, pos, max_token;
    uint32_t line, column;
    const char *error;
    ZssToken token;
} ZssLexer;

/* max_token is a caller-selected bound, including quotes or '$'. */
int ZssLex_Init(ZssLexer *lex, const char *source, size_t size, size_t max_token);
int ZssLex_Next(ZssLexer *lex);
/* ASCII case-insensitive identifier comparison; raw source is never changed. */
int ZssLex_Is(const ZssLexer *lex, const char *identifier);
#endif
