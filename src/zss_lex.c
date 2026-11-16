/* Port addition: native ZSS lexer, first increment of the ZSS plan.
 * No INI normalization: '#' comments and ';' delimiters have distinct roles.
 * This does not yet compile or execute ZSS files. */
#include "mugen.h"
#include "zss_lex.h"

static int letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int digit(unsigned char c) { return c >= '0' && c <= '9'; }
static unsigned char lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}
static unsigned char peek(const ZssLexer *l)
{
    return l->pos < l->size ? (unsigned char)l->source[l->pos] : 0;
}
static void advance(ZssLexer *l)
{
    unsigned char c = peek(l);
    l->pos++;
    if (c == '\r' || c == '\n') {
        if (c == '\r' && l->pos < l->size && l->source[l->pos] == '\n') l->pos++;
        l->line++; l->column = 1;
    } else {
        l->column++;
    }
}
static int fail(ZssLexer *l, const char *message)
{
    l->error = message;
    l->token.kind = ZSS_ERROR;
    l->token.length = l->pos - l->token.offset;
    return ZSS_ERROR;
}
int ZssLex_Init(ZssLexer *l, const char *source, size_t size, size_t max_token)
{
    if (l == NULL) return 0;
    memset(l, 0, sizeof(*l));
    l->source = source; l->size = size; l->max_token = max_token;
    l->line = l->column = 1;
    if ((source == NULL && size != 0) || size > UINT32_MAX - 1u || max_token == 0) {
        l->error = "Invalid ZSS source or token bound";
        return 0;
    }
    if (size >= 3 && (unsigned char)source[0] == 0xef &&
        (unsigned char)source[1] == 0xbb && (unsigned char)source[2] == 0xbf) l->pos = 3;
    return 1;
}
int ZssLex_Next(ZssLexer *l)
{
    unsigned char c, next;
    int kind, floating = 0;
    if (l == NULL) return ZSS_ERROR;
    if (l->error != NULL) return ZSS_ERROR;
    for (;;) {
        c = peek(l);
        if (l->pos == l->size) break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            advance(l); continue;
        }
        if (c != '#') break;
        while (l->pos < l->size && peek(l) != '\r' && peek(l) != '\n' && peek(l) != 0) advance(l);
        /* Embedded NUL is an error even in comments. */
        if (l->pos < l->size && peek(l) == 0) break;
    }
    l->token = (ZssToken){ZSS_EOF, l->pos, 0, l->line, l->column};
    if (l->pos == l->size) return ZSS_EOF;
    c = peek(l);
    if (c == 0) return fail(l, "Embedded NUL in ZSS source");
    next = l->pos + 1 < l->size ? (unsigned char)l->source[l->pos + 1] : 0;
    if (c == '"') {
        advance(l);
        /* Match upstream readString: the next quote closes the string.
         * Keep backslashes raw; no new escape grammar is introduced here. */
        while (l->pos < l->size && peek(l) != '"') {
            if (peek(l) == 0 || peek(l) == '\r' || peek(l) == '\n') return fail(l, "Unterminated ZSS string");
            advance(l);
            if (l->pos - l->token.offset > l->max_token) return fail(l, "ZSS token too long");
        }
        if (l->pos == l->size) return fail(l, "Unterminated ZSS string");
        advance(l); kind = ZSS_STRING;
    } else if (letter(c) || c == '$') {
        kind = c == '$' ? ZSS_LOCAL : ZSS_IDENT;
        advance(l);
        if (c == '$' && !letter(peek(l))) return fail(l, "Missing ZSS local name");
        while (l->pos < l->size && (letter(peek(l)) || digit(peek(l)) || peek(l) == '.')) {
            advance(l);
            if (l->pos - l->token.offset > l->max_token) return fail(l, "ZSS token too long");
        }
    } else if (digit(c) || (c == '.' && digit(next))) {
        while (digit(peek(l))) advance(l);
        if (peek(l) == '.') {
            floating = 1; advance(l);
            while (digit(peek(l))) advance(l);
        }
        if (peek(l) == 'e' || peek(l) == 'E') {
            floating = 1; advance(l);
            if (peek(l) == '+' || peek(l) == '-') advance(l);
            if (!digit(peek(l))) return fail(l, "Malformed ZSS exponent");
            while (digit(peek(l))) advance(l);
        }
        kind = floating ? ZSS_FLOAT : ZSS_INT;
    } else {
        kind = c;
        if (c == ':' && next == '=') kind = ZSS_ASSIGN;
        else if (c == '!' && next == '=') kind = ZSS_NE;
        else if (c == '>' && next == '=') kind = ZSS_GE;
        else if (c == '<' && next == '=') kind = ZSS_LE;
        else if (c == '&' && next == '&') kind = ZSS_AND;
        else if (c == '|' && next == '|') kind = ZSS_OR;
        else if (c == '^' && next == '^') kind = ZSS_XOR;
        else if (c == '*' && next == '*') kind = ZSS_POWER;
        else if (strchr("[]{}();:,.=<>!~&|^+-*/%", c) == NULL) return fail(l, "Invalid ZSS character");
        advance(l);
        if (kind >= ZSS_ASSIGN) advance(l);
    }
    l->token.length = l->pos - l->token.offset;
    if (l->token.length > l->max_token) return fail(l, "ZSS token too long");
    l->token.kind = kind;
    return kind;
}
int ZssLex_Is(const ZssLexer *l, const char *name)
{
    size_t i;
    if (l == NULL || name == NULL || l->token.kind != ZSS_IDENT) return 0;
    for (i = 0; i < l->token.length; i++) {
        if (name[i] == 0 || lower((unsigned char)name[i]) !=
            lower((unsigned char)l->source[l->token.offset + i])) return 0;
    }
    return name[i] == 0;
}
