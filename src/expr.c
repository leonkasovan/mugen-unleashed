/*
 * CNS expressions: parser, compiled-expression helpers and evaluator
 * (original module 0x401690-0x40369F).
 *
 * Expr_Parse is a hand-written shunting-yard parser: numbers and trigger calls
 * (parsed by Trigger_Parse, which recurses into Expr_Parse for arguments) go
 * to the output, operators through a 64-entry operator stack. Parentheses and
 * the interval forms "x = [a, b)" are parsed by recursion, the recursive call
 * stopping at the ',' or closing bracket. The postfix result is checked by
 * simulating the stack depth.
 *
 * Expr_Eval runs the postfix code on the global evaluation stack. A failed
 * operation (division by zero...) pushes an EXPR_BOTTOM entry instead of a
 * value; a stack underflow or a type mismatch is fatal.
 *
 * Quirks kept from the original (see the functions for details):
 * - '~' is tested as the character '~' (0x7E) instead of EXPR_OP_BNOT in the
 *   right-associativity test, the "only unary operators after an operator"
 *   test and the evaluator's unary test, so "a && ~b" does not parse and the
 *   evaluator pops two operands for ~ (stealing one from the stack or failing).
 * - var(60) := x writes fvar(0) and fvar(40) := x writes sysvar(0). PORTABILITY FIX:
 *   those two writes go through the following member instead of indexing past
 *   the end of the array; the storage and the bits are the same.
 * - int 0 ** 0 yields an uninitialized value (0 here) and the INT_MAX warning.
 * - Expr_Halve reallocates the type array with 4 bytes per token.
 * - PORTABILITY FIX: failed/empty groups are rejected and pending sub-parse
 *   tokens are released. The original reused stale interval counts or an
 *   uninitialized end pointer. Every output write is also room-checked.
 * - pow() goes through MSVCR90's _CIpow via mugen.exe's import thunk, as in the original.
 */
#include "mugen.h"

/* What the parser read last (decides whether '-' is unary, '(' is a group...). */
enum {
    PARSE_LAST_NONE = 0,
    PARSE_LAST_NUMBER = 1,
    PARSE_LAST_OP = 2,
    PARSE_LAST_INTERVAL = 3,
    PARSE_LAST_TRIGGER = 4,
    PARSE_LAST_UNUSED = 5,      /* tested but never set */
    PARSE_LAST_GROUP = 6
};

#define EXPR_INT_MAX_D  2147483647.0
#define EXPR_INT_MIN_D  (-2147483648.0)

/* pow() as the original calls it (through _CIpow, operands on the x87 stack). */
static double Expr_Pow(double x, double y)
{
    return pow(x, y);
}

/*
 * 0x00401690: base ** exp for ints. Returns 0, or 1 / -1 when the result was
 * clamped to INT_MAX / INT_MIN. 0 ** 0 returns 1 without writing *out.
 */
int Expr_IntPow(int *out, int base, int exp)
{
    double r;

    if (base == 0 && exp == 0) {
        return 1;
    }
    r = Expr_Pow((double)base, (double)exp);
    if (r > EXPR_INT_MAX_D) {
        *out = 0x7FFFFFFF;
        return 1;
    }
    if (r < EXPR_INT_MIN_D) {
        *out = (int)0x80000000;
        return -1;
    }
    *out = (int)r;
    return 0;
}

/* 0x004016F0: '~' is tested as the character, not EXPR_OP_BNOT (original bug). */
int Expr_IsRightAssoc(int op)
{
    return op == EXPR_OP_ASSIGN || op == EXPR_OP_FASSIGN || op == EXPR_OP_NOT || op == '~' || op == EXPR_OP_NEG;
}

/* 0x00401720 */
int Expr_Precedence(int op)
{
    switch (op) {
    case EXPR_OP_OR:
        return 1;
    case EXPR_OP_XOR:
        return 2;
    case EXPR_OP_AND:
        return 3;
    case EXPR_OP_BOR:
        return 4;
    case EXPR_OP_BXOR:
        return 5;
    case EXPR_OP_BAND:
        return 6;
    case EXPR_OP_ASSIGN:
    case EXPR_OP_FASSIGN:
        return 7;
    case EXPR_OP_EQ:
    case EXPR_OP_NE:
        return 8;
    case EXPR_OP_IN_OO:
    case EXPR_OP_IN_OC:
    case EXPR_OP_IN_CO:
    case EXPR_OP_IN_CC:
        return 9;
    case EXPR_OP_GT:
    case EXPR_OP_LT:
    case EXPR_OP_GE:
    case EXPR_OP_LE:
        return 10;
    case EXPR_OP_ADD:
    case EXPR_OP_SUB:
        return 11;
    case EXPR_OP_MUL:
    case EXPR_OP_DIV:
    case EXPR_OP_MOD:
        return 12;
    case EXPR_OP_POW:
        return 13;
    case EXPR_OP_NOT:
    case EXPR_OP_NEG:
    case EXPR_OP_BNOT:
        return 14;
    case EXPR_OP_LPAREN:
    case EXPR_OP_RPAREN:
        return 15;
    default:
        return 0;
    }
}

/* 0x004017F0: mode 1 = int operands, 2 = float. Bitwise ops, :=, % and ~ need ints. */
int Expr_OpAcceptsMode(int op, int mode)
{
    switch (op) {
    case EXPR_OP_EQ:
    case EXPR_OP_NE:
    case EXPR_OP_GT:
    case EXPR_OP_LT:
    case EXPR_OP_GE:
    case EXPR_OP_LE:
    case EXPR_OP_IN_OO:
    case EXPR_OP_IN_OC:
    case EXPR_OP_IN_CO:
    case EXPR_OP_IN_CC:
    case EXPR_OP_AND:
    case EXPR_OP_OR:
    case EXPR_OP_NOT:
    case EXPR_OP_XOR:
    case EXPR_OP_POW:
    case EXPR_OP_NEG:
    case EXPR_OP_FASSIGN:
    case EXPR_OP_ADD:
    case EXPR_OP_SUB:
    case EXPR_OP_MUL:
    case EXPR_OP_DIV:
    case EXPR_OP_LPAREN:
    case EXPR_OP_RPAREN:
        return 1;
    case EXPR_OP_BAND:
    case EXPR_OP_BOR:
    case EXPR_OP_BXOR:
    case EXPR_OP_ASSIGN:
    case EXPR_OP_BNOT:
    case EXPR_OP_MOD:
        return mode == 1;
    default:
        return 0;
    }
}

/* 0x00401840: the operator at s, or 0. '-' is always EXPR_OP_SUB here. */
int Expr_ReadOp(const char *s)
{
    switch (s[0]) {
    case '!':
        return s[1] == '=' ? EXPR_OP_NE : EXPR_OP_NOT;
    case '%':
        return EXPR_OP_MOD;
    case '&':
        return s[1] == '&' ? EXPR_OP_AND : EXPR_OP_BAND;
    case '(':
        return EXPR_OP_LPAREN;
    case ')':
        return EXPR_OP_RPAREN;
    case '*':
        return s[1] == '*' ? EXPR_OP_POW : EXPR_OP_MUL;
    case '+':
        return EXPR_OP_ADD;
    case '-':
        return EXPR_OP_SUB;
    case '/':
        return EXPR_OP_DIV;
    case ':':
        return s[1] == '=' ? EXPR_OP_ASSIGN : 0;
    case '<':
        return s[1] == '=' ? EXPR_OP_LE : EXPR_OP_LT;
    case '=':
        return EXPR_OP_EQ;
    case '>':
        return s[1] == '=' ? EXPR_OP_GE : EXPR_OP_GT;
    case '^':
        return s[1] == '^' ? EXPR_OP_XOR : EXPR_OP_BXOR;
    case '|':
        return s[1] == '|' ? EXPR_OP_OR : EXPR_OP_BOR;
    case '~':
        return EXPR_OP_BNOT;
    default:
        return 0;
    }
}

/*
 * 0x004019D0: parses s into postfix tokens (at most max). With end != NULL
 * parsing stops at a ',' or a closing bracket and *end is set to it; without,
 * a closing bracket is an error. Returns the token count, 0 on errors (with a
 * message in g_libraryErrorText for some of them), -1 when s is NULL.
 */
static int Expr_ParseBody(const char *s, ExprValue *out_tokens, char *out_types, CharData *owner, int max,
                          const char **end, int want, ExprCompileCtx *ctx)
{
    /*
     * The output and the sub-parse buffers are adjacent, as in the original:
     * popping operators does not check for room, so the output can run past
     * EXPR_MAX_TOKENS into the sub-parse buffers (only on the way to a failure).
     */
    ExprValue all_tokens[2 * EXPR_MAX_TOKENS];
    char all_types[2 * EXPR_MAX_TOKENS];
    ExprValue *tokens = all_tokens;
    char *types = all_types;
    ExprValue *sub_tokens = all_tokens + EXPR_MAX_TOKENS;
    char *sub_types = all_types + EXPR_MAX_TOKENS;
    char ops[EXPR_OP_STACK_SIZE];
    const char *cur;
    const char *trig_end;
    const char *bracket_end;
    const char *interval_end;
    /*
     * Only set by a successful sub-parse, but read after "()" (an empty
     * sub-parse fails): the original reads an uninitialized stack slot there.
     */
    const char *paren_end = s;
    char *num_end;
    int n = 0;
    int last = PARSE_LAST_NONE;
    int top = -1;
    int top_prec = 0;
    int count1 = 0, count2 = 0, count;
    int op, prec, interval_op, depth, i;
    char c;

    if (s == NULL) {
        return -1;
    }
    cur = s;
    if (strlen(cur) == 0) {
        goto fail;
    }
    for (;;) {
        if (end != NULL && *cur == ',') {
            break;
        }
        if (*cur == '$') {
            const char *name = ++cur;
            int slot;
            if (ctx == NULL || ctx->resolve_local == NULL ||
                !(isalpha((unsigned char)*cur) || *cur == '_')) goto fail;
            while (isalnum((unsigned char)*cur) || *cur == '_') cur++;
            slot = ctx->resolve_local(ctx->user, name, (size_t)(cur-name));
            if (slot < 0 || slot >= 256 || n >= max) goto fail;
            tokens[n].i = slot; types[n++] = EXPR_LOCAL;
            last = PARSE_LAST_NUMBER;
            goto next;
        }
        if ((Char_IsDigit(cur[0]) || (cur[0] == '-' && Char_IsDigit(cur[1])) ||
             (cur[0] == '.' && Char_IsDigit(cur[1])) || (cur[0] == '-' && cur[1] == '.' && Char_IsDigit(cur[2]))) &&
            last != PARSE_LAST_NUMBER && last != PARSE_LAST_TRIGGER && last != PARSE_LAST_GROUP) {
            /* A number; '-' only starts one where an operand is expected. */
            last = PARSE_LAST_NUMBER;
            if (n >= max) {
                goto fail;
            }
            tokens[n].i = Expr_Strtol32(cur, &num_end);
            if (*num_end == '.' || (cur[0] == '-' && cur[1] == '.')) {
                tokens[n].f = (float)strtod(cur, &num_end);
                types[n] = EXPR_FLOAT;
            } else {
                types[n] = EXPR_INT;
            }
            n++;
            cur = num_end;
            goto next;
        }
        op = Expr_ReadOp(cur);
        /* Port policy: the legacy ~ parser/runtime disagree on stack arity.
         * Keep CNS's quirk; reject it in ZSS until a proper unary op is added. */
        if (ctx!=NULL && op==EXPR_OP_BNOT) {
            snprintf(g_libraryErrorText,sizeof(g_libraryErrorText),"Bitwise not is not enabled in ZSS expressions");
            goto fail;
        }
        if (op != 0 && *cur != '(' && *cur != '[' && *cur != ')' && *cur != ']') {
            if (op == EXPR_OP_SUB) {
                if (last != PARSE_LAST_NONE && last != PARSE_LAST_UNUSED && last != PARSE_LAST_OP) {
                    goto push_op;
                }
                op = EXPR_OP_NEG;
            }
            /* Only unary operators may follow an operator ('~' tested as the character). */
            if (last == PARSE_LAST_OP && (char)op != EXPR_OP_NOT && (char)op != '~' && (char)op != EXPR_OP_NEG) {
                goto fail;
            }
            if (op == EXPR_OP_ASSIGN) {
                /* var(n) := x / fvar(n) := x: drop the trigger token, keeping its argument n. */
                Trigger *trig;

                if (last != PARSE_LAST_TRIGGER) {
                    goto fail;
                }
                trig = tokens[n - 1].trig;
                if (trig->id != TRIGGER_FVAR && trig->id != TRIGGER_VAR) {
                    goto fail;
                }
                if (trig->redirect != 0) {
                    goto fail;
                }
                if (trig->id == TRIGGER_FVAR) {
                    op = EXPR_OP_FASSIGN;
                }
                if (Expr_Precedence((char)op) < top_prec) {
                    c = ops[top];
                    if (c != EXPR_OP_LPAREN && c != EXPR_OP_ASSIGN && c != EXPR_OP_FASSIGN) {
                        goto fail;
                    }
                }
                Trigger_Free(trig);
                n--;
            }
        push_op:
            last = PARSE_LAST_OP;
            {
                int right = Expr_IsRightAssoc(op) ||
                    (ctx!=NULL && (op==EXPR_OP_AND || op==EXPR_OP_OR));

                prec = top_prec;
                for (;;) {
                    int op_prec = Expr_Precedence(op);

                    if (right ? op_prec >= prec : op_prec > prec) {
                        break;
                    }
                    if (top == -1) {
                        break;
                    }
                    /* PORTABILITY FIX: the original wrote here without a room check. */
                    if (n >= max) {
                        goto fail;
                    }
                    tokens[n].i = ops[top--];
                    types[n] = EXPR_OP;
                    n++;
                    prec = top > -1 ? Expr_Precedence(ops[top]) : 0;
                }
            }
            top++;
            if (top >= EXPR_OP_STACK_SIZE) {
                sprintf(g_libraryErrorText, "Parser stack overflow error");
                if (ctx!=NULL) goto fail;
                Error_Exit();
            }
            ops[top] = (char)op;
            top_prec = Expr_Precedence(op);
            switch (op) {
            case EXPR_OP_EQ:
            case EXPR_OP_GT:
            case EXPR_OP_LT:
            case EXPR_OP_NOT:
            case EXPR_OP_BAND:
            case EXPR_OP_BOR:
            case EXPR_OP_BXOR:
            case EXPR_OP_NEG:
            case EXPR_OP_BNOT:
            case EXPR_OP_ADD:
            case EXPR_OP_SUB:
            case EXPR_OP_MUL:
            case EXPR_OP_DIV:
            case EXPR_OP_MOD:
            case EXPR_OP_LPAREN:
            case EXPR_OP_RPAREN:
                cur += 1;
                break;
            case EXPR_OP_NE:
            case EXPR_OP_GE:
            case EXPR_OP_LE:
            case EXPR_OP_AND:
            case EXPR_OP_OR:
            case EXPR_OP_XOR:
            case EXPR_OP_POW:
            case EXPR_OP_ASSIGN:
            case EXPR_OP_FASSIGN:
                cur += 2;
                break;
            default:
                break;
            }
            goto next;
        }
        c = *cur;
        if (c == '[') {
            /* "= [a, b]", "= [a, b)" (also with != ) */
            if (last != PARSE_LAST_OP || top == -1 || (ops[top] != EXPR_OP_EQ && ops[top] != EXPR_OP_NE)) {
                goto fail;
            }
            last = PARSE_LAST_INTERVAL;
            count1 = Expr_ParseCtx(cur + 1, sub_tokens, sub_types, owner, max - n, &bracket_end, EXPR_WANT_ANY, ctx);
            if (count1 == 0 || *bracket_end != ',') {
                goto fail;
            }
            count2 = Expr_ParseCtx(bracket_end + 1, sub_tokens + count1, sub_types + count1, owner, max - count1 - n,
                                &bracket_end, EXPR_WANT_ANY, ctx);
            if (count2 == 0) {
                goto fail;
            }
            if (*bracket_end != ')' && *bracket_end != ']') {
                goto fail;
            }
            {
                const char *p = bracket_end + 1;

                while (Char_IsSpace(*p) && *p != '\0') {
                    p++;
                }
                if (*p != ')' && *p != '\0') {
                    goto fail;
                }
            }
            interval_op = (*bracket_end != ')') + EXPR_OP_IN_CO;
            interval_end = bracket_end;
        } else if (c == '(') {
            /* A group "(x)", or the interval forms "= (a, b)", "= (a, b]". */
            if (last == PARSE_LAST_NUMBER) {
                goto fail;
            }
            count1 = Expr_ParseCtx(cur + 1, sub_tokens, sub_types, owner, max - n, &paren_end, EXPR_WANT_ANY, ctx);
            if (count1 != 0) {
                if (*paren_end != ',') {
                    if (*paren_end != ')') {
                        goto fail;
                    }
                    if (n + count1 > max) {
                        goto fail;
                    }
                    memcpy(&tokens[n], sub_tokens, count1 * sizeof(ExprValue));
                    memcpy(&types[n], sub_types, count1);
                    n += count1;
                    count1 = 0; /* ownership moved into tokens */
                    last = PARSE_LAST_GROUP;
                    cur = paren_end + 1;
                    goto next;
                }
                count2 = Expr_ParseCtx(paren_end + 1, sub_tokens + count1, sub_types + count1, owner,
                                    max - count1 - n, &paren_end, EXPR_WANT_ANY, ctx);
                if (count2 == 0) {
                    goto fail;
                }
                if (*paren_end != ')' && *paren_end != ']') {
                    goto fail;
                }
                {
                    const char *p = paren_end + 1;

                    while (Char_IsSpace(*p)) {
                        p++;
                    }
                    if (*p != ')' && *p != '\0') {
                        goto fail;
                    }
                }
            }
            /* Portability fix: an empty/failed group owns no tokens and must
             * not reuse a previous interval's count or trigger pointers. */
            if (count1 == 0) {
                goto fail;
            }
            if (last == PARSE_LAST_UNUSED || last == PARSE_LAST_GROUP || top == -1 ||
                (ops[top] != EXPR_OP_EQ && ops[top] != EXPR_OP_NE)) {
                goto fail;
            }
            last = PARSE_LAST_INTERVAL;
            interval_op = (*paren_end != ')') + EXPR_OP_IN_OO;
            interval_end = paren_end;
        } else if (c == ')' || c == ']') {
            if (end == NULL) {
                goto fail;
            }
            break;
        } else if (isalnum(c) || c == '_') {
            count = Trigger_ParseCtx(cur, owner, &tokens[n], &types[n], max - n, &trig_end, ctx);
            if (count == 0) {
                snprintf(g_libraryErrorText, sizeof(g_libraryErrorText), "Died parsing %s", s);
                goto fail;
            }
            n += count;
            last = PARSE_LAST_TRIGGER;
            if (n > max) {
                sprintf(g_libraryErrorText, "Overabundance.");
                goto fail;
            }
            cur = trig_end;
            goto next;
        } else if (Char_IsSpace(*cur)) {
            cur++;
            goto next;
        } else {
            goto fail;
        }

        /* Interval: pop the operators that bind tighter, then output a, b and the interval operator. */
        prec = Expr_Precedence(interval_op);
        if (prec <= top_prec) {
            for (;;) {
                if (top == -1) {
                    top_prec = 0;
                    break;
                }
                c = ops[top--];
                if (n >= max) {
                    goto fail;
                }
                tokens[n].i = c;
                types[n] = EXPR_OP;
                n++;
                top_prec = top > -1 ? Expr_Precedence(ops[top]) : 0;
                if (prec > top_prec) {
                    break;
                }
            }
        }
        count = count1 + count2;
        if (n + count > max) {
            goto fail;
        }
        memcpy(&tokens[n], sub_tokens, count * sizeof(ExprValue));
        memcpy(&types[n], sub_types, count);
        n += count;
        count1 = count2 = 0; /* ownership moved into tokens */
        cur = interval_end + 1;
        if (n >= max) {
            goto fail;
        }
        tokens[n].i = interval_op;
        types[n] = EXPR_OP;
        n++;

    next:
        if (*cur == '\0') {
            break;
        }
    }

    /* End of the expression: flush the operator stack. */
    for (i = top; i > -1; i--) {
        if (n >= max) {
            goto fail;
        }
        tokens[n].i = ops[i];
        types[n] = EXPR_OP;
        n++;
    }
    if (n == 0) {
        goto fail;
    }
    if ((types[0] == EXPR_INT || types[0] == EXPR_FLOAT) && n == 1) {
        if (want == EXPR_WANT_FLOAT) {
            if (types[0] == EXPR_INT) {
                types[0] = EXPR_FLOAT;
                tokens[0].f = (float)tokens[0].i;
            }
        } else if (want == EXPR_WANT_INT && types[0] == EXPR_FLOAT) {
            sprintf(g_libraryErrorText, "Expected int value and read float.");
            goto fail;
        }
    }
    /* Check that the postfix code leaves exactly one value. */
    depth = 0;
    for (i = 0; i < n; i++) {
        switch (types[i]) {
        case EXPR_INT:
        case EXPR_FLOAT:
        case EXPR_LOCAL:
            depth++;
            break;
        case EXPR_OP:
            switch (tokens[i].i) {
            case EXPR_OP_EQ:
            case EXPR_OP_NE:
            case EXPR_OP_GT:
            case EXPR_OP_LT:
            case EXPR_OP_GE:
            case EXPR_OP_LE:
            case EXPR_OP_AND:
            case EXPR_OP_OR:
            case EXPR_OP_XOR:
            case EXPR_OP_POW:
            case EXPR_OP_BAND:
            case EXPR_OP_BOR:
            case EXPR_OP_BXOR:
            case EXPR_OP_ASSIGN:
            case EXPR_OP_FASSIGN:
            case EXPR_OP_ADD:
            case EXPR_OP_SUB:
            case EXPR_OP_MUL:
            case EXPR_OP_DIV:
            case EXPR_OP_MOD:
                if (depth < 2) {
                    goto malformed;
                }
                depth--;
                break;
            case EXPR_OP_IN_OO:
            case EXPR_OP_IN_OC:
            case EXPR_OP_IN_CO:
            case EXPR_OP_IN_CC:
                /* x a b IN, then the = / != token, which is not checked */
                if (depth < 3) {
                    goto malformed;
                }
                depth -= 2;
                i++;
                break;
            case EXPR_OP_NOT:
            case EXPR_OP_NEG:
            case EXPR_OP_BNOT:
                if (depth == 0) {
                    goto malformed;
                }
                break;
            default:
                goto malformed;
            }
            break;
        case EXPR_TRIGGER:
            depth -= Trigger_ArgCount(tokens[i].trig->id);
            if (depth < 0) {
                goto malformed;
            }
            depth++;
            break;
        default:
            goto malformed;
        }
    }
    if (depth == 1) {
        if (end != NULL) {
            *end = cur;
        }
        memcpy(out_tokens, tokens, n * sizeof(ExprValue));
        memcpy(out_types, types, n);
        return n;
    }

malformed:
    snprintf(g_libraryErrorText, sizeof(g_libraryErrorText), "Malformed expression encountered: %s", s);
fail:
    /* Successful sub-parses not yet copied into the output still belong here. */
    for (i = 0; i < count1 + count2; i++) {
        if (sub_types[i] == EXPR_TRIGGER) {
            Trigger_Free(sub_tokens[i].trig);
        }
    }
    for (i = 0; i < n; i++) {
        if (types[i] == EXPR_TRIGGER) {
            Trigger_Free(tokens[i].trig);
        }
    }
    return 0;
}

/*
 * The Expr_Parse calls in progress. Every kind of nesting -- a group, an
 * interval, a trigger argument, a redirection argument -- recurses through
 * Expr_Parse, and the 128-token limit does not bound an expression whose
 * nesting adds no tokens ("((((1))))"), so a long enough run of parentheses
 * would exhaust the stack (about 1.5 KiB per call on i386, 2.6 KiB on x86-64).
 * PORTABILITY FIX: nesting is capped, deeper input is a parse error.
 */
static int expr_parse_depth;

/* 0x004019D0; the body is Expr_ParseBody, this wrapper only adds the depth guard. */
int Expr_Parse(const char *s, ExprValue *out_tokens, char *out_types, CharData *owner, int max, const char **end,
               int want)
{
    return Expr_ParseCtx(s,out_tokens,out_types,owner,max,end,want,NULL);
}
int Expr_ParseCtx(const char *s, ExprValue *out_tokens, char *out_types, CharData *owner, int max, const char **end,
                  int want, ExprCompileCtx *ctx)
{
    int n;
    int *depth = ctx != NULL ? &ctx->depth : &expr_parse_depth;

    if (*depth >= EXPR_MAX_DEPTH) {
        snprintf(g_libraryErrorText, sizeof(g_libraryErrorText), "Expression too deeply nested");
        return 0;
    }
    /* The internal buffers hold EXPR_MAX_TOKENS, irrespective of a caller's
     * larger output capacity. Preserve the public -1 result for NULL input. */
    if (max > EXPR_MAX_TOKENS) {
        max = EXPR_MAX_TOKENS;
    }
    (*depth)++;
    n = Expr_ParseBody(s, out_tokens, out_types, owner, max, end, want, ctx);
    (*depth)--;
    return n;
}

/* Port addition: turn the verified postfix tree into forward-only short-circuit
 * code. Parsing remains shared; only explicit-context compilation lowers it.
 * Scalar jump displacements survive clone/copy and appended scaling operations. */
#define EXPR_ZSS_MAX_CODE (EXPR_MAX_TOKENS*2)
static int expr_arity(int op)
{
    if (op>=EXPR_OP_IN_OO && op<=EXPR_OP_IN_CC) return 3;
    if (op==EXPR_OP_NOT || op==EXPR_OP_NEG) return 1;
    return 2;
}
static int expr_flow_depth(int *depths, int at, int value)
{
    if (value<0 || value>EXPR_STACK_SIZE) return 0;
    if (depths[at]>=0 && depths[at]!=value) return 0;
    depths[at]=value;return 1;
}
/* All control-flow edges must agree on operand depth. Interval compare tags
 * are payload words, not instruction boundaries or legal jump destinations. */
static int expr_verify_lazy(const ExprValue *code, const char *types, int n)
{
    int depths[EXPR_ZSS_MAX_CODE+1], boundary[EXPR_ZSS_MAX_CODE+1]={0};
    int i,d,need,width,target;
    if (n<1 || n>EXPR_ZSS_MAX_CODE) return 0;
    for (i=0;i<=n;i++) depths[i]=-1;
    for (i=0;i<n;i++) {
        boundary[i]=1;
        if (types[i]==EXPR_OP && code[i].i>=EXPR_OP_IN_OO && code[i].i<=EXPR_OP_IN_CC) {
            if (i+1>=n || types[i+1]!=EXPR_OP ||
                (code[i+1].i!=EXPR_OP_EQ && code[i+1].i!=EXPR_OP_NE)) return 0;
            i++;
        }
    }
    boundary[n]=1;depths[0]=0;
    for (i=0;i<n;i++) {
        if (!boundary[i]) continue;
        d=depths[i];if (d<0) return 0;
        width=1;
        switch (types[i]) {
        case EXPR_INT:case EXPR_FLOAT:case EXPR_LOCAL:
            if (!expr_flow_depth(depths,i+1,d+1)) return 0;
            break;
        case EXPR_TRIGGER:
            need=Trigger_ArgCount(code[i].trig->id);
            if (d<need || !expr_flow_depth(depths,i+1,d-need+1)) return 0;
            break;
        case EXPR_OP:
            if (code[i].i<EXPR_OP_EQ || code[i].i>EXPR_OP_MOD) return 0;
            need=expr_arity(code[i].i);
            if (need==3) width=2;
            if (d<need || !expr_flow_depth(depths,i+width,d-need+1)) return 0;
            break;
        case EXPR_SC_AND:case EXPR_SC_OR:
            /* Bounds are checked before adding the untrusted scalar offset. */
            if (d<1 || code[i].i<1 || code[i].i>n-i) return 0;
            target=i+code[i].i;
            if (!boundary[target] || !expr_flow_depth(depths,target,d) ||
                !expr_flow_depth(depths,i+1,d-1)) return 0;
            break;
        case EXPR_BOOL:
            if (d<1 || !expr_flow_depth(depths,i+1,d)) return 0;
            break;
        default:return 0;
        }
    }
    return depths[n]==1;
}
/* Dependency classification, not speculative evaluation: mathematical calls
 * and constant cond/ifelse trees can normalize their logical parent, while
 * variables, redirections, random and assignments must remain dynamic. */
static int expr_pure(const Expr *e, unsigned depth);
static int expr_pure_trigger(const Trigger *t, unsigned depth)
{
    int i;
    if (depth>=EXPR_MAX_DEPTH || t->redirect!=TRIGGER_REDIRECT_NONE) return 0;
    if (t->id==TRIGGER_COND) {
        if (t->data==NULL) return 0;
        for (i=0;i<3;i++) if (!expr_pure((const Expr *)t->data+i,depth+1)) return 0;
        return 1;
    }
    return (t->id>=TRIGGER_SIN && t->id<=TRIGGER_CEIL) ||
           t->id==TRIGGER_LOG || t->id==TRIGGER_PI || t->id==TRIGGER_E || t->id==TRIGGER_IFELSE;
}
static int expr_pure(const Expr *e, unsigned depth)
{
    int i;
    if (depth>=EXPR_MAX_DEPTH) return 0;
    if (e->count==EXPR_CONST) return e->t.type==EXPR_INT || e->t.type==EXPR_FLOAT;
    if (e->count<0 || e->count>EXPR_ZSS_MAX_CODE) return 0;
    for (i=0;i<e->count;i++) {
        if (e->t.types[i]==EXPR_LOCAL) return 0;
        if (e->t.types[i]==EXPR_TRIGGER && !expr_pure_trigger(e->u.tokens[i].trig,depth+1)) return 0;
        if (e->t.types[i]==EXPR_OP && (e->u.tokens[i].i==EXPR_OP_ASSIGN || e->u.tokens[i].i==EXPR_OP_FASSIGN)) return 0;
    }
    return 1;
}
static int expr_lower_lazy(ExprValue *tokens, char *types, int n, ExprValue *out, char *kinds)
{
    struct Tree {int at,width,arity,pure,child[3];} nodes[EXPR_MAX_TOKENS];
    struct Emit {int node,child,jump;} frames[EXPR_MAX_TOKENS];
    int roots[EXPR_MAX_TOKENS], used=0, top=0, i,j,arity,width,pure,size=0,branch;
    struct Tree *node;
    struct Emit *frame;
    for (i=0;i<n;i++) {
        arity=0;width=1;pure=types[i]!=EXPR_LOCAL && types[i]!=EXPR_TRIGGER;
        if (types[i]==EXPR_TRIGGER) {
            arity=Trigger_ArgCount(tokens[i].trig->id);pure=expr_pure_trigger(tokens[i].trig,0);
        }
        else if (types[i]==EXPR_OP) {
            arity=expr_arity(tokens[i].i);
            if (arity==3) width=2;
            if (tokens[i].i==EXPR_OP_ASSIGN || tokens[i].i==EXPR_OP_FASSIGN) pure=0;
        } else if (types[i]!=EXPR_INT && types[i]!=EXPR_FLOAT && types[i]!=EXPR_LOCAL) return 0;
        if (arity>3 || top<arity || used>=EXPR_MAX_TOKENS || i+width>n) return 0;
        node=&nodes[used];node->at=i;node->width=width;node->arity=arity;
        for (j=0;j<arity;j++) {node->child[j]=roots[top-arity+j];pure&=nodes[node->child[j]].pure;}
        node->pure=pure;top-=arity;roots[top++]=used++;i+=width-1;
    }
    if (top!=1) return 0;
    frames[0]=(struct Emit){roots[0],0,-1};top=1;
    while (top) {
        frame=&frames[top-1];node=&nodes[frame->node];
        branch=types[node->at]==EXPR_OP &&
               (tokens[node->at].i==EXPR_OP_AND || tokens[node->at].i==EXPR_OP_OR);
        if (frame->child<node->arity) {
            if (branch && frame->child==1) {
                if (size>=EXPR_ZSS_MAX_CODE) return 0;
                frame->jump=size;out[size].i=0;
                kinds[size++]=tokens[node->at].i==EXPR_OP_AND ? EXPR_SC_AND : EXPR_SC_OR;
            }
            j=node->child[frame->child++];
            if (top>=EXPR_MAX_TOKENS) return 0;
            frames[top++]=(struct Emit){j,0,-1};continue;
        }
        if (branch) {
            out[frame->jump].i=size-frame->jump;
            /* Upstream non-folded paths retain the selected operand. Pure
             * numeric subtrees normalize as its constant logical fold does. */
            if (node->pure) {
                if (size>=EXPR_ZSS_MAX_CODE) return 0;
                out[size].i=0;kinds[size++]=EXPR_BOOL;
            }
        } else {
            if (size+node->width>EXPR_ZSS_MAX_CODE) return 0;
            for (j=0;j<node->width;j++) {out[size]=tokens[node->at+j];kinds[size++]=types[node->at+j];}
        }
        top--;
    }
    return expr_verify_lazy(out,kinds,size) ? size : 0;
}

/* Publish the arrays together; callers retain parsed trigger ownership until
 * this succeeds. Lazy scratch storage is allocated on the C stack only on the
 * explicit-context logical path, not on ordinary CNS compilations. */
static int expr_publish(Expr *e, const ExprValue *tokens, const char *types, int n)
{
    ExprValue *code=Mem_Calloc(n,sizeof(*code));
    char *kinds=Mem_Calloc(n,1);
    if (code==NULL || kinds==NULL) {free(code);free(kinds);return 0;}
    memcpy(code,tokens,n*sizeof(*code));memcpy(kinds,types,n);
    e->u.tokens=code;e->t.types=kinds;e->count=n;return 1;
}
static int expr_compile_lazy(Expr *e, ExprValue *tokens, char *types, int n)
{
    ExprValue lowered[EXPR_ZSS_MAX_CODE];
    char kinds[EXPR_ZSS_MAX_CODE];
    int size=expr_lower_lazy(tokens,types,n,lowered,kinds);
    return size>0 && expr_publish(e,lowered,kinds,size);
}

/*
 * 0x004023D0: compiles s into e (whose old contents are not freed). A single
 * number is stored as a constant. Returns 1 when s is NULL, leaving e alone.
 */
int Expr_Compile(const char *s, Expr *e, CharData *owner, const char **end, int want)
{
    return Expr_CompileCtx(s,e,owner,end,want,NULL);
}
int Expr_CompileCtx(const char *s, Expr *e, CharData *owner, const char **end, int want, ExprCompileCtx *ctx)
{
    ExprValue tokens[EXPR_MAX_TOKENS];
    char types[EXPR_MAX_TOKENS];
    int n, i, lazy=0, ok;

    if (s == NULL) {
        return 1;
    }
    n = Expr_ParseCtx(s, tokens, types, owner, EXPR_MAX_TOKENS, end, want, ctx);
    if (n == 0) {
        return 0;
    }
    if (n == 1 && (types[0] == EXPR_INT || types[0] == EXPR_FLOAT)) {
        e->count = EXPR_CONST;
        e->u.value = tokens[0];
        e->t.type = types[0];
        owner->expr_count++;
        return 1;
    }
    if (ctx!=NULL) for (i=0;i<n;i++) if (types[i]==EXPR_OP &&
        (tokens[i].i==EXPR_OP_AND || tokens[i].i==EXPR_OP_OR)) {lazy=1;break;}
    ok=lazy ? expr_compile_lazy(e,tokens,types,n) : expr_publish(e,tokens,types,n);
    if (!ok) {
        for (i=0;i<n;i++) if (types[i]==EXPR_TRIGGER) Trigger_Free(tokens[i].trig);
        return 0;
    }
    owner->expr_count++;
    return 1;
}

/* 0x004024F0: frees the tokens (and their triggers) and clears e. */
void Expr_Free(Expr *e)
{
    int i;

    if (e->count != EXPR_CONST && e->count != 0) {
        for (i = 0; i < e->count; i++) {
            if (e->t.types[i] == EXPR_TRIGGER) {
                Trigger_Free(e->u.tokens[i].trig);
            }
        }
        free(e->t.types);
        free(e->u.tokens);
    }
    e->u.tokens = NULL;
    e->t.types = NULL;
    e->count = 0;
}

/* 0x00402550: frees a keyed array of Exprs (KeyedArray_First/Next/Free inlined). */
void ExprList_Free(KeyedArray *list)
{
    Expr *e;
    int i;

    if (list == NULL) {
        return;
    }
    if (list->count != 0) {
        list->iter = list->first;
        e = (Expr *)(list->data + list->elem_size * list->first);
        while (e != NULL) {
            Expr_Free(e);
            i = list->iter;
            if (i >= list->last) {
                break;
            }
            do {
                i++;
            } while (list->slots[i].used == 0);
            list->iter = i;
            e = (Expr *)(list->data + list->elem_size * i);
        }
    }
    if (list->data != NULL) {
        free(list->data);
    }
    if (list->slots != NULL) {
        free(list->slots);
    }
    free(list);
}

/* 0x004025D0: only the type byte is written (the rest of e->t keeps old bytes). */
void Expr_SetInt(Expr *e, int value)
{
    if (e->count != EXPR_CONST && e->count != 0) {
        Expr_Free(e);
    }
    e->count = EXPR_CONST;
    e->u.value.i = value;
    e->t.type = EXPR_INT;
}

/* 0x00402600 */
void Expr_SetFloat(Expr *e, float value)
{
    if (e->count != EXPR_CONST && e->count != 0) {
        Expr_Free(e);
    }
    e->count = EXPR_CONST;
    e->u.value.f = value;
    e->t.type = EXPR_FLOAT;
}

/* 0x00402630: copies src into dst; trigger tokens are shared (see Expr_Clone). */
int Expr_Copy(Expr *dst, const Expr *src)
{
    Expr tmp;

    /* Portability fix: self-copy and allocation failure must not free src or
     * lose dst. Trigger ownership is shallow here; Expr_Clone deep-copies it. */
    if (dst == src) {
        return 1;
    }
    memset(&tmp, 0, sizeof(tmp));
    if (src->count == EXPR_CONST || src->count == 0) {
        tmp = *src;
    } else {
        tmp.u.tokens = Mem_Calloc(src->count, sizeof(ExprValue));
        tmp.t.types = Mem_Calloc(src->count, 1);
        if (tmp.u.tokens == NULL || tmp.t.types == NULL) {
            free(tmp.u.tokens);
            free(tmp.t.types);
            return 0;
        }
        tmp.count = src->count;
        memcpy(tmp.u.tokens, src->u.tokens, src->count * sizeof(ExprValue));
        memcpy(tmp.t.types, src->t.types, src->count);
    }
    Expr_Free(dst);
    *dst = tmp;
    return 1;
}

/* 0x004026E0: Expr_Copy with copies of the triggers. */
int Expr_Clone(Expr *dst, const Expr *src)
{
    Trigger *trig;
    int i;

    if (dst == src) {
        return 1;
    }
    if (!Expr_Copy(dst, src)) {
        /* Existing clone callers treat allocation failure as fatal; do not
         * report a successful clone after a partial allocation. */
        g_outOfMemory = 1;
        snprintf(g_libraryErrorText, sizeof(g_libraryErrorText), "Out of memory cloning expression");
        Error_Exit();
        return 0;
    }
    if (dst->count != EXPR_CONST && dst->count != 0) {
        for (i = 0; i < dst->count; i++) {
            if (dst->t.types[i] == EXPR_TRIGGER) {
                trig = calloc(1, sizeof(Trigger));
                if (trig == NULL) {
                    g_outOfMemory = 1;
                    sprintf(g_libraryErrorText, "Uh-oh. Alloc error");
                    Error_Exit();
                }
                Trigger_Copy(dst->u.tokens[i].trig, trig);
                dst->u.tokens[i].trig = trig;
            }
        }
    }
    return 1;
}

/* 0x00402770: e *= k (folded into constants, else appended as "k *"). */
void Expr_MulFloat(Expr *e, float k)
{
    int count = e->count;

    if (count == 0) {
        return;
    }
    if (count == EXPR_CONST) {
        if (e->t.type == EXPR_INT) {
            e->t.type = EXPR_FLOAT;
            /* The int is not rounded to float before the multiplication (fild; fmul). */
            e->u.value.f = (float)((double)e->u.value.i * k);
        } else {
            e->u.value.f = e->u.value.f * k;
        }
        return;
    }
    e->count = count + 2;
    e->u.tokens = realloc(e->u.tokens, e->count * sizeof(ExprValue));
    e->t.types = realloc(e->t.types, e->count);
    if (e->u.tokens == NULL || e->t.types == NULL) {
        Error_Exit();
    }
    e->u.tokens[e->count - 2].f = k;
    e->t.types[e->count - 2] = EXPR_FLOAT;
    e->u.tokens[e->count - 1].i = EXPR_OP_MUL;
    e->t.types[e->count - 1] = EXPR_OP;
}

/* 0x00402810: e /= 2 (folded into constants, else appended as "2 /"). */
void Expr_Halve(Expr *e)
{
    int count = e->count;

    if (count == 0) {
        return;
    }
    if (count == EXPR_CONST) {
        if (e->t.type == EXPR_INT) {
            e->u.value.i = e->u.value.i / 2;
        } else {
            e->u.value.f = (float)(e->u.value.f * 0.5);
        }
        return;
    }
    e->count = count + 2;
    e->u.tokens = realloc(e->u.tokens, e->count * sizeof(ExprValue));
    e->t.types = realloc(e->t.types, e->count * 4);    /* 4 bytes per type: original bug, harmless */
    if (e->u.tokens == NULL || e->t.types == NULL) {
        sprintf(g_libraryErrorText, "Can't realloc memory.");
        Error_Exit();
    }
    e->u.tokens[e->count - 2].i = 2;
    e->t.types[e->count - 2] = EXPR_INT;
    e->u.tokens[e->count - 1].i = EXPR_OP_DIV;
    e->t.types[e->count - 1] = EXPR_OP;
}

#define EXPR_FATAL_MALFORMED "%4i: Fatal: Player %s (%i) in state %i: malformed expression"

/* 0x004028C0: evaluates e as an int; floats are truncated, with a warning if warn. */
int Expr_EvalInt(Player *p, const Expr *e, int warn)
{
    return Expr_EvalIntCtx(p,e,warn,NULL);
}
int Expr_EvalIntCtx(Player *p, const Expr *e, int warn, ExprEvalCtx *ctx)
{
    float fval;
    int ival;
    int ret = Expr_EvalCtx(p, e, &ival, &fval, ctx);

    if (ret == 0) {
        sprintf(g_libraryErrorText, EXPR_FATAL_MALFORMED, g_game->frames, p->name, p->id, p->state_no);
        Error_Exit();
    }
    if (ret == 2) {
        ival = Ftol((double)fval);
        if (warn) {
            TextRing_Printf(g_game->clipboard,
                            "%i Warning: expression truncated to integer:\nplayer %s (%i) in state %i: %f",
                            g_game->frames, p->name, p->id, p->state_no, (double)fval);
        }
        return ival;
    }
    if (ret == 3) {
        return 0;
    }
    return ival;
}

/* 0x004029A0: evaluates e as a float (bottom gives 0). */
float Expr_EvalFloat(Player *p, const Expr *e)
{
    return Expr_EvalFloatCtx(p,e,NULL);
}
float Expr_EvalFloatCtx(Player *p, const Expr *e, ExprEvalCtx *ctx)
{
    float fval;
    int ival;
    int ret = Expr_EvalCtx(p, e, &ival, &fval, ctx);

    if (ret == 0) {
        sprintf(g_libraryErrorText, EXPR_FATAL_MALFORMED, g_game->frames, p->name, p->id, p->state_no);
        Error_Exit();
    }
    if (ret == 1) {
        return (float)ival;
    }
    if (ret == 3) {
        fval = 0.0f;
    }
    return fval;
}

/*
 * 0x00402A20: evaluates e for player p. Returns 1 with an int in *iout, 2 with
 * a float in *fout, 3 for bottom (*iout = 0), 0 for a bad constant type.
 * Malformed code (stack underflow, type mismatch, leftover values) is fatal.
 *
 * Binary operators pop b then a; mode is 1 when both are ints, 2 when either
 * is a float (fa, fb hold both as floats then). Comparisons and logical
 * operators push ints.
 */
int Expr_Eval(Player *p, const Expr *e, int *iout, float *fout)
{
    return Expr_EvalCtx(p,e,iout,fout,NULL);
}
static int expr_script_truth(ExprValue value, int type)
{
    return type==EXPR_INT ? value.i!=0 : type==EXPR_FLOAT ?
           !_isnan(value.f) && value.f!=0.0f : 0;
}
int Expr_EvalCtx(Player *p, const Expr *e, int *iout, float *fout, ExprEvalCtx *ctx)
{
    float fa = 0.0f, fb = 0.0f, fx = 0.0f;
    ExprValue a, b, x, result;
    int at = 0, bt, xt, mode = 0, rtype;
    int op = 0;                 /* stale in the bottom handler for bad token types, as in the original */
    int pow_out = 0;            /* uninitialized stack slot in the original (0 ** 0) */
    int count = e->count;
    int base = g_exprStackTop;
    int i, r, ret, idx, next_op, in, out, top;
    const char *tp;
    const ExprValue *slot;

    a.i = 0;
    x.i = 0;
    result.f = 0.0f;
    if (count == 0) {
        *iout = 0;
        return 1;
    }
    if (count == EXPR_CONST) {
        switch (e->t.type) {
        case EXPR_INT:
            *iout = e->u.value.i;
            return 1;
        case EXPR_FLOAT:
            *fout = e->u.value.f;
            return 2;
        }
        return 0;
    }
    for (i = 0; i < count; i++) {
        tp = &e->t.types[i];
        switch (*tp) {
        case EXPR_INT:
        case EXPR_FLOAT:
            ExprStack_Push(e->u.tokens[i].i, *tp);
            continue;
        case EXPR_SC_AND:case EXPR_SC_OR:case EXPR_BOOL:
            if (ctx==NULL || g_exprStackTop<=base) {g_exprStackTop=base;return 0;}
            top=g_exprStackTop;rtype=g_exprStackType[top];
            if (rtype!=EXPR_INT && rtype!=EXPR_FLOAT && rtype!=EXPR_BOTTOM) {g_exprStackTop=base;return 0;}
            r=expr_script_truth(g_exprStackValue[top],rtype);
            if (*tp==EXPR_BOOL) {
                g_exprStackValue[top].i=r;g_exprStackType[top]=EXPR_INT;continue;
            }
            idx=e->u.tokens[i].i;
            if (idx<1 || idx>count-i) {g_exprStackTop=base;return 0;}
            if ((*tp==EXPR_SC_AND && !r) || (*tp==EXPR_SC_OR && r)) i+=idx-1;
            else g_exprStackTop--;
            continue;
        case EXPR_LOCAL:
            idx = e->u.tokens[i].i;
            if (ctx == NULL || ctx->values == NULL || ctx->types == NULL ||
                idx < 0 || (size_t)idx >= ctx->count ||
                (ctx->types[idx] != EXPR_INT && ctx->types[idx] != EXPR_FLOAT && ctx->types[idx] != EXPR_BOTTOM)) {
                g_exprStackTop = base; return 0;
            }
            ExprStack_Push(ctx->values[idx].i,ctx->types[idx]);
            continue;
        case EXPR_TRIGGER:
            Trigger_EvalCtx(p, e->u.tokens[i].trig, ctx);
            continue;
        case EXPR_OP:
            break;
        default:
            goto bottom;
        }

        slot = &e->u.tokens[i];
        op = slot->i;
        if (g_exprStackTop <= -1) {
            goto malformed;
        }
        b = g_exprStackValue[g_exprStackTop];
        bt = g_exprStackType[g_exprStackTop];
        g_exprStackTop--;
        mode = 1;
        if (bt == EXPR_FLOAT) {
            fb = b.f;
            mode = 2;
        }
        /* Unary operators pop one operand ('~' tested as the character: ~ pops two). */
        if (op != EXPR_OP_NOT && op != '~' && op != EXPR_OP_NEG) {
            if (!ExprStack_Pop(&a.i, &at)) {
                goto malformed;
            }
            if (mode == 2) {
                fa = at == EXPR_INT ? (float)a.i : a.f;
            } else if (at == EXPR_FLOAT) {
                mode = 2;
                fa = a.f;
                fb = (float)b.i;
            }
        }
        /* Never true (tests the token type, which is EXPR_OP): bottom operands are not propagated here. */
        if (*tp < 7 && *tp > 10) {
            if (at == EXPR_BOTTOM) {
                ExprStack_Push(a.i, EXPR_BOTTOM);
                continue;
            }
            if (bt == EXPR_BOTTOM) {
                ExprStack_Push(b.i, EXPR_BOTTOM);
                continue;
            }
        }
        if (!Expr_OpAcceptsMode(op, mode)) {
            Error_Printf("Type mismatch detected: %d\n", slot->i);
            goto malformed;
        }
        switch (op) {
        case EXPR_OP_ADD:
            if (mode == 1) {
                /* PORTABILITY FIX: int overflow is undefined in C; the reference wraps
                   modulo 2^32, which the unsigned computation reproduces. */
                result.i = (int)((unsigned int)a.i + (unsigned int)b.i);
                ExprStack_Push(result.i, EXPR_INT);
            } else {
                result.f = fa + fb;
                ExprStack_Push(result.i, EXPR_FLOAT);
            }
            break;
        case EXPR_OP_SUB:
            if (mode == 1) {
                result.i = (int)((unsigned int)a.i - (unsigned int)b.i);
                ExprStack_Push(result.i, EXPR_INT);
            } else {
                result.f = fa - fb;
                ExprStack_Push(result.i, EXPR_FLOAT);
            }
            break;
        case EXPR_OP_MUL:
            if (mode == 1) {
                result.i = (int)((unsigned int)a.i * (unsigned int)b.i);
                ExprStack_Push(result.i, EXPR_INT);
            } else {
                result.f = fa * fb;
                ExprStack_Push(result.i, EXPR_FLOAT);
            }
            break;
        case EXPR_OP_DIV:
            if (mode == 1) {
                if (b.i == 0) {
                    goto bottom;
                }
                if (b.i == -1 && a.i == (int)0x80000000) {
                    /* PORTABILITY FIX: idiv traps (SIGFPE) here, the value is not
                       representable; push bottom like the other failed operations. */
                    Player_Warn(p, "Integer division overflow");
                    ExprStack_Push(0, EXPR_BOTTOM);
                    break;
                }
                result.i = a.i / b.i;
                ExprStack_Push(result.i, EXPR_INT);
            } else {
                if (fb == 0.0f) {
                    goto bottom;
                }
                result.f = fa / fb;
                ExprStack_Push(result.i, EXPR_FLOAT);
            }
            break;
        case EXPR_OP_MOD:
            if (b.i == 0) {
                goto bottom;
            }
            /* PORTABILITY FIX: INT32_MIN % -1 traps; mathematically it is 0. */
            result.i = b.i == -1 ? 0 : a.i % b.i;
            ExprStack_Push(result.i, EXPR_INT);
            break;
        case EXPR_OP_POW:
            if (mode == 1) {
                if (b.i >= 0) {
                    ret = Expr_IntPow(&pow_out, a.i, b.i);
                    result.i = pow_out;
                    ExprStack_Push(result.i, EXPR_INT);
                    if (ret != 0) {
                        if (ret > 0) {
                            Player_Warn(p, "int exponentiation clamped to INT_MAX");
                        } else {
                            Player_Warn(p, "int exponentiation clamped to INT_MIN");
                        }
                    }
                    break;
                }
                fa = (float)a.i;
                fb = (float)b.i;
            }
            result.f = (float)Expr_Pow(fa, fb);
            if (_isnan(result.f)) {
                goto bottom;
            }
            ExprStack_Push(result.i, EXPR_FLOAT);
            break;
        case EXPR_OP_ASSIGN:
            /* Only reachable with int operands (or bottom ones, read as floats). */
            if (bt == EXPR_INT) {
                result.i = b.i;
            } else {
                result.i = Ftol((double)b.f);
                Player_Warn(p, "Float truncated to int in var assignment");
            }
            ExprStack_Push(result.i, EXPR_INT);
            idx = at == EXPR_FLOAT ? Ftol((double)fa) : a.i;
            if ((unsigned int)idx < PLAYER_VAR_COUNT) {
                p->var[idx] = result.i;
            } else if ((unsigned int)idx == PLAYER_VAR_COUNT) {
                /* var(60) is fvar(0): same storage as the original's index past the
                   end of the array, without the out-of-bounds access. */
                memcpy(&p->fvar[0], &result.i, sizeof(result.i));
            } else {
                Player_Warn(p, "Assignment to illegal variable ignored");
            }
            break;
        case EXPR_OP_FASSIGN:
            result.f = bt == EXPR_INT ? (float)b.i : b.f;
            ExprStack_Push(result.i, EXPR_FLOAT);
            idx = at == EXPR_FLOAT ? Ftol((double)fa) : a.i;
            if ((unsigned int)idx < PLAYER_FVAR_COUNT) {
                p->fvar[idx] = result.f;
            } else if ((unsigned int)idx == PLAYER_FVAR_COUNT) {
                /* fvar(40) is sysvar(0): same storage as the original's index past
                   the end of the array, without the out-of-bounds access. */
                memcpy(&p->sysvar[0], &result.f, sizeof(result.f));
            } else {
                Player_Warn(p, "Assignment to illegal variable ignored");
            }
            break;
        case EXPR_OP_EQ:
        case EXPR_OP_NE:
            if (mode == 1) {
                r = a.i == b.i;
            } else {
                r = fa == fb;
            }
            if (op != EXPR_OP_EQ) {
                r = !r;
            }
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            break;
        case EXPR_OP_GT:
        case EXPR_OP_LE:
            if (mode == 1) {
                r = a.i > b.i;
            } else {
                r = fa > fb;
            }
            if (op != EXPR_OP_GT) {
                r = !r;
            }
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            break;
        case EXPR_OP_LT:
        case EXPR_OP_GE:
            if (mode == 1) {
                r = a.i < b.i;
            } else {
                r = fa < fb;
            }
            if (op != EXPR_OP_LT) {
                r = !r;
            }
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            break;
        case EXPR_OP_OR:
            if (mode == 1) {
                r = a.i != 0 || b.i != 0;
            } else {
                r = fa != 0.0f || fb != 0.0f;
            }
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            break;
        case EXPR_OP_AND:
            if (mode == 1) {
                r = a.i != 0 && b.i != 0;
            } else {
                r = fa != 0.0f && fb != 0.0f;
            }
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            break;
        case EXPR_OP_XOR:
            if (mode == 1) {
                r = a.i != 0 ? b.i == 0 : b.i != 0;
            } else {
                r = fa != 0.0f ? fb == 0.0f : fb != 0.0f;
            }
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            break;
        case EXPR_OP_NOT:
            if (mode == 1) {
                r = b.i == 0;
            } else {
                r = fb == 0.0f;
            }
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            break;
        case EXPR_OP_BAND:
            result.i = a.i & b.i;
            ExprStack_Push(result.i, EXPR_INT);
            break;
        case EXPR_OP_BOR:
            result.i = a.i | b.i;
            ExprStack_Push(result.i, EXPR_INT);
            break;
        case EXPR_OP_BXOR:
            result.i = a.i ^ b.i;
            ExprStack_Push(result.i, EXPR_INT);
            break;
        case EXPR_OP_BNOT:
            result.i = ~b.i;
            ExprStack_Push(result.i, EXPR_INT);
            break;
        case EXPR_OP_NEG:
            if (mode == 1) {
                result.i = (int)(0u - (unsigned int)b.i);
                ExprStack_Push(result.i, EXPR_INT);
            } else {
                result.f = -fb;
                ExprStack_Push(result.i, EXPR_FLOAT);
            }
            break;
        case EXPR_OP_IN_OO:
        case EXPR_OP_IN_OC:
        case EXPR_OP_IN_CO:
        case EXPR_OP_IN_CC:
            /* x a b IN followed by = or !=: pops x too and skips the = / != token. */
            if (tp[1] != EXPR_OP) {
                goto malformed;
            }
            next_op = slot[1].i;
            if (next_op != EXPR_OP_EQ && next_op != EXPR_OP_NE) {
                goto malformed;
            }
            if (!ExprStack_Pop(&x.i, &xt)) {
                goto malformed;
            }
            if (xt == EXPR_BOTTOM) {
                goto bottom;
            }
            in = next_op == EXPR_OP_EQ;
            out = !in;
            if (xt == EXPR_FLOAT) {
                fx = x.f;
            }
            if (mode == 1) {
                if (xt == EXPR_FLOAT) {
                    fb = (float)b.i;
                    mode = 2;
                    fa = (float)a.i;
                } else {
                    switch (op) {
                    case EXPR_OP_IN_OO:
                        r = x.i > a.i && x.i < b.i ? in : out;
                        break;
                    case EXPR_OP_IN_OC:
                        r = x.i > a.i && x.i <= b.i ? in : out;
                        break;
                    case EXPR_OP_IN_CO:
                        r = x.i >= a.i && x.i < b.i ? in : out;
                        break;
                    default:
                        r = x.i >= a.i && x.i <= b.i ? in : out;
                        break;
                    }
                    goto interval_done;
                }
            } else if (mode == 2 && xt == EXPR_INT) {
                fx = (float)x.i;
            }
            switch (op) {
            case EXPR_OP_IN_OO:
                r = fa < fx && fb > fx ? in : out;
                break;
            case EXPR_OP_IN_OC:
                r = fa < fx && fb >= fx ? in : out;
                break;
            case EXPR_OP_IN_CO:
                r = fa <= fx && fb > fx ? in : out;
                break;
            default:
                r = fa <= fx && fb >= fx ? in : out;
                break;
            }
        interval_done:
            result.i = r;
            ExprStack_Push(r, EXPR_INT);
            i++;
            break;
        default:
            break;
        }
        continue;

    bottom:
        /* The operation has no value: push bottom. */
        switch (op) {
        case EXPR_OP_IN_OO:
        case EXPR_OP_IN_OC:
        case EXPR_OP_IN_CO:
        case EXPR_OP_IN_CC:
            i++;
            break;
        case EXPR_OP_DIV:
            Player_Warn(p, "Divison by zero");
            break;
        case EXPR_OP_MOD:
            Player_Warn(p, "Modulus by zero");
            break;
        case EXPR_OP_POW:
            Player_Warn(p, "Invalid exponentiation");
            break;
        case EXPR_OP_ASSIGN:
        case EXPR_OP_FASSIGN:
            Player_Warn(p, "Short-circuited var assign ignored");
            break;
        }
        result.i = 0;
        if (g_exprStackTop >= EXPR_STACK_SIZE - 1) {
            sprintf(g_libraryErrorText, "Expression stack overflow.");
            Error_Exit();
        }
        g_exprStackTop++;
        g_exprStackValue[g_exprStackTop].i = 0;
        g_exprStackType[g_exprStackTop] = EXPR_BOTTOM;
    }

    top = g_exprStackTop;
    if (top - base != 1) {
        goto malformed;
    }
    if (top > -1) {
        result = g_exprStackValue[top];
        rtype = g_exprStackType[top];
        g_exprStackTop = top - 1;
    } else {
        rtype = mode;           /* unreachable: top - base == 1 means top >= 0 */
    }
    if (rtype == EXPR_INT) {
        *iout = result.i;
        return 1;
    }
    if (rtype == EXPR_FLOAT) {
        *fout = result.f;
        return 2;
    }
    if (rtype == EXPR_BOTTOM) {
        *iout = result.i;
        return 3;
    }
    return 0;

malformed:
    sprintf(g_libraryErrorText, "\n" EXPR_FATAL_MALFORMED, g_game->frames, p->name, p->id, p->state_no);
    Error_Exit();
}
