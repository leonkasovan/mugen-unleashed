/* Port addition: owned ZSS state programs and structured body execution.
 * Kept separate from original StateDefs/Player layouts. This prototype rejects
 * features that need the pending expression-context and runtime integrations. */
#include "mugen.h"
#include "zss_params.h"
#include "zss_statedef.h"
#include "zss_program.h"
#include "zss_binding.h"
#include <limits.h>
#include "ftol.h"
#define NO_NODE UINT32_MAX
#define MAX_NODES 1024u
#define MAX_STATES 64u
#define MAX_FUNCTIONS 64u
#define MAX_MODULES 64u
#define MAX_CALL_DEPTH 32u
#define MAX_FRAMES 256u
#define MAX_DEPTH 32u
#define MAX_LOOP 2500u
#define MAX_STEPS 100000u
/* Source punctuation is not a postfix token or an operator-stack entry. */
#define MAX_EXPR_SOURCE_TOKENS (4 * EXPR_MAX_TOKENS)
enum {NODE_CONTROLLER, NODE_IF, NODE_WHILE, NODE_EXPRESSION, NODE_BREAK, NODE_CONTINUE, NODE_FOR, NODE_LET, NODE_CASE, NODE_CALL};

typedef struct ZssCall {
    char name[64];
    Expr *arguments;
    int *returns;
    uint32_t argument_count, return_count;
} ZssCall;

typedef struct ZssNode {
    int kind, local;
    int persistence, persistent_slot;
    unsigned ihp, has_ihp;
    uint32_t next, child, otherwise;
    Expr condition;
    Expr range[3];
    StateCtrl controller;
    ZssCall *call;
} ZssNode;
typedef struct ZssState {
    int number;
    uint32_t first, locals;
    StateDef properties;
} ZssState;
typedef struct ZssFunction {
    char name[64];
    uint32_t first, locals, arguments, returns;
} ZssFunction;
struct ZssProgram {
    unsigned references;
    ZssNode *nodes;
    ZssState *states;
    ZssFunction *functions;
    uint32_t node_count, node_capacity, state_count, state_capacity;
    uint32_t function_count, function_capacity, call_count;
};
/* Collection-owned, stable library handle; modules are kept in load order.
 * Programs never point back here, so retained modules cannot form a cycle. */
typedef struct ZssLibrary {
    unsigned references;
    uint32_t count;
    ZssProgram **modules;
} ZssLibrary;
typedef struct ZssParser {
    ZssLexer lex;
    ZssProgram *program;
    CharData *owner;
    ZssDiagnostic *diagnostic;
    ExprCompileCtx context;
    ZssToken locals[256];
    unsigned local_count;
    size_t switch_bytes;
    unsigned ihp, in_function, persistence_count;
    int state_number;
} ZssParser;
static int error(ZssParser *p, const char *message)
{
    if (p->diagnostic != NULL) {
        p->diagnostic->message=p->lex.error != NULL ? p->lex.error : message;
        p->diagnostic->line=p->lex.token.line;
        p->diagnostic->column=p->lex.token.column;
    }
    return 0;
}
static int next(ZssParser *p) { return ZssLex_Next(&p->lex); }
static int resolve_local(void *user, const char *name, size_t length)
{
    ZssParser *p=user;
    unsigned i;
    size_t j;
    for (i=p->local_count;i>0;) {
        --i;
        const ZssToken *t=&p->locals[i];
        if (t->length!=length) continue;
        for (j=0;j<length;j++) if (tolower((unsigned char)name[j]) !=
            tolower((unsigned char)p->lex.source[t->offset+j])) break;
        if (j==length) return (int)i;
    }
    return -1;
}
static int valid_name(ZssParser *p, const ZssToken *t)
{
    size_t i;
    if (t->kind!=ZSS_IDENT) return error(p,"Expected an identifier");
    for (i=0;i<t->length;i++) {
        unsigned char c=(unsigned char)p->lex.source[t->offset+i];
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' ||
              (i>0 && c>='0' && c<='9'))) return error(p,"Invalid local or function name");
    }
    return 1;
}
static int copy_name(ZssParser *p, char name[64])
{
    size_t i;
    if (!valid_name(p,&p->lex.token)) return 0;
    if (p->lex.token.length>=64) return error(p,"ZSS function name limit (63 characters)");
    for (i=0;i<p->lex.token.length;i++)
        name[i]=(char)tolower((unsigned char)p->lex.source[p->lex.token.offset+i]);
    name[i]=0;return 1;
}
/* '_' discards a binding without consuming a slot. Names enter scope before
 * the initializer is compiled, matching uninitialized self-read as bottom. */
static int declare_local(ZssParser *p)
{
    ZssToken t=p->lex.token;
    int slot;
    if (t.kind!=ZSS_IDENT) {error(p,"Expected a local variable name");return -1;}
    /* Port fix: generic identifiers permit dots for controller parameters,
     * but a local must use the same spelling accepted by expression reads. */
    if (!valid_name(p,&t)) return -1;
    if (t.length==1 && p->lex.source[t.offset]=='_') return -2;
    slot=resolve_local(p,p->lex.source+t.offset,t.length);
    if (slot>=0) return slot;
    if (p->local_count>=256) {error(p,"ZSS local variable limit (256)");return -1;}
    p->locals[p->local_count]=t;return (int)p->local_count++;
}
/* Bounded vectors use allocate/copy/publish, so OOM never loses the old arena. */
static void *grow(void *old, uint32_t *capacity, uint32_t count, size_t bytes, uint32_t limit)
{
    void *data;
    uint32_t size;
    if (count < *capacity) return old;
    if (count >= limit) return NULL;
    size=*capacity ? *capacity*2 : 8;
    if (size>limit) size=limit;
    data=calloc(size,bytes);
    if (data==NULL) return NULL;
    if (count) memcpy(data,old,(size_t)count*bytes);
    free(old);*capacity=size;
    return data;
}
static uint32_t node(ZssParser *p)
{
    ZssProgram *s=p->program;
    uint32_t index;
    ZssNode *nodes=grow(s->nodes,&s->node_capacity,s->node_count,sizeof(*s->nodes),MAX_NODES);
    if (nodes==NULL) {
        error(p,"ZSS node limit or allocation failure");return NO_NODE;
    }
    s->nodes=nodes;
    index=s->node_count++;
    s->nodes[index].local=-1;
    s->nodes[index].persistence=1;
    s->nodes[index].next=s->nodes[index].child=s->nodes[index].otherwise=NO_NODE;
    return index;
}
static void relocate(ZssDiagnostic *d, const ZssToken *start)
{
    if (d==NULL) return;
    if (d->line==1) d->column+=start->column-1;
    d->line+=start->line-1;
}
/* Return a complete header or controller slice, leaving its closing token. */
static int enclosed(ZssParser *p, int header)
{
    unsigned depth=1;
    int kind;
    while ((kind=next(p))>0) {
        if (header && (kind==ZSS_AND || kind==ZSS_OR))
            return error(p,"Logical StateDef properties need pending header contexts");
        if ((header && (kind=='[' || kind=='(')) || (!header && kind=='{')) depth++;
        if ((header && (kind==']' || kind==')')) || (!header && kind=='}')) {
            if (--depth==0) return kind==(header?']':'}') || error(p,"Wrong closing delimiter");
        }
        if (depth>MAX_DEPTH) return error(p,"ZSS delimiter nesting limit");
    }
    return error(p,"Unclosed ZSS header or controller");
}
static int body(ZssParser *p, unsigned depth, unsigned loops, int labels, uint32_t *first);
static int branch(ZssParser *p, unsigned depth, unsigned loops, int loop, uint32_t *index, uint32_t *end_line)
{
    uint32_t id=node(p), child=NO_NODE, other=NO_NODE;
    char *expression, *write;
    const char *end;
    int tokens=0, kind;
    if (id==NO_NODE) return 0;
    if (depth>=MAX_DEPTH) return error(p,"ZSS block nesting limit");
    p->program->nodes[id].kind=loop ? NODE_WHILE : NODE_IF;
    expression=calloc(1,p->lex.size*2+1);
    if (expression==NULL) return error(p,"Out of memory compiling condition");
    write=expression;
    while ((kind=next(p))!='{') {
        if (kind<=0 || kind=='}') {
            free(expression);return error(p,"Invalid or unsupported if condition");
        }
        if (++tokens>=MAX_EXPR_SOURCE_TOKENS) {free(expression);return error(p,"Condition token limit");}
        if (tokens>1 && !(write[-1]=='-' && (kind==ZSS_INT || kind==ZSS_FLOAT))) *write++=' ';
        memcpy(write,p->lex.source+p->lex.token.offset,p->lex.token.length);write+=p->lex.token.length;
    }
    end=expression;
    if (!tokens || !Expr_CompileCtx(expression,&p->program->nodes[id].condition,p->owner,&end,EXPR_WANT_ANY,&p->context) || *end) {
        free(expression);return error(p,"Condition rejected by MUGEN expression compiler");
    }
    free(expression);
    next(p);
    if (!body(p,depth+1,loops+(loop ? 1u : 0u),0,&child)) return 0;
    if (p->lex.token.kind!='}') return error(p,"Expected '}' after if body");
    *end_line=p->lex.token.line;next(p);
    if (!loop && ZssLex_Is(&p->lex,"else")) {
        next(p);
        if (ZssLex_Is(&p->lex,"if")) {
            if (!branch(p,depth+1,loops,0,&other,end_line)) return 0;
        } else {
            if (p->lex.token.kind!='{') return error(p,"Expected if or '{' after else");
            next(p);
            if (!body(p,depth+1,loops,0,&other)) return 0;
            if (p->lex.token.kind!='}') return error(p,"Expected '}' after else body");
            *end_line=p->lex.token.line;next(p);
        }
    }
    /* Recursion may grow the node arena; retain indices rather than pointers. */
    p->program->nodes[id].child=child;p->program->nodes[id].otherwise=other;
    *index=id;return 1;
}
/* Keep minus adjacent to a following numeric token during lowering: the legacy
 * compiler recognizes -2147483648 as a signed literal, but clamps the positive
 * token in "- 2147483648" before applying unary negation. */
/* Assignment expressions reuse the MUGEN numeric evaluator; the statement's
 * result is discarded; a let statement instead stores the numeric result.
 * Logical operands lower through the explicit expression context. */
static int assignment(ZssParser *p, int local, uint32_t *id, uint32_t *end_line)
{
    char *text=calloc(1,p->lex.size*2+1), *write=text;
    const char *end;
    int tokens=0, has_assignment=0, kind;
    uint32_t index;
    if (text==NULL) return error(p,"Out of memory compiling assignment");
    while ((kind=p->lex.token.kind)!=';') {
        if (kind<=0 || kind=='{' || kind=='}') {
            free(text);return error(p,"Assignment must end with ';'");
        }
        if (++tokens>=MAX_EXPR_SOURCE_TOKENS) {free(text);return error(p,"Assignment token limit");}
        if (kind==ZSS_ASSIGN) has_assignment=1;
        if (tokens>1 && !(write[-1]=='-' && (kind==ZSS_INT || kind==ZSS_FLOAT))) *write++=' ';
        memcpy(write,p->lex.source+p->lex.token.offset,p->lex.token.length);write+=p->lex.token.length;
        next(p);
    }
    if (local==-1 && !has_assignment) {free(text);return error(p,"Expression with unused value");}
    index=node(p);
    if (index==NO_NODE) {free(text);return 0;}
    p->program->nodes[index].kind=local==-1 ? NODE_EXPRESSION : NODE_LET;
    p->program->nodes[index].local=local;
    end=text;
    if (!Expr_CompileCtx(text,&p->program->nodes[index].condition,p->owner,&end,EXPR_WANT_ANY,&p->context) || *end) {
        free(text);return error(p,"Assignment rejected by MUGEN compiler");
    }
    free(text);*id=index;*end_line=p->lex.token.line;next(p);return 1;
}
/* Parse call arguments as shared expressions, rather than splitting on every
 * comma: MUGEN redirects, intervals and nested trigger arguments own commas. */
static int call_statement(ZssParser *p, const int *returns, unsigned count,
                          uint32_t *id, uint32_t *end_line)
{
    uint32_t index=node(p), capacity=0;
    ZssCall *call;
    Expr *arguments;
    char *text, *write;
    const char *cursor, *end;
    unsigned depth=1, tokens;
    int kind;
    ZssLexer scan;
    if (index==NO_NODE) return 0;
    p->program->nodes[index].kind=NODE_CALL;
    call=calloc(1,sizeof(*call));
    if (call==NULL) return error(p,"Out of memory compiling call");
    p->program->nodes[index].call=call;p->program->call_count++;
    next(p);
    if (!copy_name(p,call->name)) return 0;
    if (count) {
        call->returns=calloc(count,sizeof(*call->returns));
        if (call->returns==NULL) return error(p,"Out of memory compiling call results");
        memcpy(call->returns,returns,count*sizeof(*returns));call->return_count=count;
    }
    if (next(p)!='(') return error(p,"Expected '(' after function name");
    text=calloc(1,p->lex.size*2+1);
    if (text==NULL) return error(p,"Out of memory compiling call arguments");
    write=text;next(p);
    while (depth) {
        kind=p->lex.token.kind;
        if (kind<=0 || kind==';' || kind=='{' || kind=='}') {
            free(text);return error(p,"Unclosed function argument list");
        }
        if (kind=='(' || kind=='[') depth++;
        if (kind==')' || kind==']') {
            if (--depth==0) {
                if (kind!=')') {free(text);return error(p,"Expected ')' after call arguments");}
                break;
            }
        }
        if (write!=text && !(write[-1]=='-' && (kind==ZSS_INT || kind==ZSS_FLOAT))) *write++=' ';
        memcpy(write,p->lex.source+p->lex.token.offset,p->lex.token.length);write+=p->lex.token.length;
        next(p);
    }
    cursor=text;
    while (*cursor) {
        arguments=grow(call->arguments,&capacity,call->argument_count,sizeof(*arguments),256);
        if (arguments==NULL) {free(text);return error(p,"Call argument limit (256) or allocation failure");}
        call->arguments=arguments;
        end=cursor;
        if (!Expr_CompileCtx(cursor,&arguments[call->argument_count++],p->owner,&end,EXPR_WANT_ANY,&p->context) ||
            end==cursor || (*end && *end!=',')) {
            free(text);return error(p,"Call argument rejected by MUGEN expression compiler");
        }
        /* Keep the same per-expression lexical bound as other ZSS statements. */
        tokens=0;
        if (!ZssLex_Init(&scan,cursor,(size_t)(end-cursor),65536)) {
            free(text);return error(p,"Invalid call argument");
        }
        while (ZssLex_Next(&scan)>0) tokens++;
        if (scan.error || tokens>=MAX_EXPR_SOURCE_TOKENS) {
            free(text);return error(p,"Call argument token limit");
        }
        cursor=end;
        if (*cursor==',') {
            cursor++;
            while (Char_IsSpace(*cursor)) cursor++;
            if (!*cursor) {free(text);return error(p,"Missing call argument after ','");}
        }
    }
    free(text);
    if (next(p)!=';') return error(p,"Expected ';' after function call");
    *id=index;*end_line=p->lex.token.line;next(p);return 1;
}
/* Port addition: inclusive integer ranges, optionally bound to a lexical local. */
static int range_loop(ZssParser *p, unsigned depth, unsigned loops, uint32_t *id, uint32_t *end_line)
{
    uint32_t index=node(p), child=NO_NODE;
    char *text, *write;
    const char *end;
    unsigned part;
    int kind, tokens, local;
    if (index==NO_NODE) return 0;
    if (depth>=MAX_DEPTH) return error(p,"ZSS block nesting limit");
    p->program->nodes[index].kind=NODE_FOR;
    next(p);
    if (p->lex.token.kind==ZSS_IDENT) {
        local=declare_local(p);if (local==-1) return 0;
        p->program->nodes[index].local=local;
        if (next(p)!='=') return error(p,"Expected '=' after for counter name");
        next(p);
    }
    for (part=0;part<3;part++) {
        text=calloc(1,p->lex.size*2+1);
        if (text==NULL) return error(p,"Out of memory compiling for range");
        write=text;tokens=0;
        while ((kind=p->lex.token.kind)!=';' && kind!='{') {
            if (kind<=0 || kind=='}') {
                free(text);return error(p,"Invalid for range expression");
            }
            if (++tokens>=MAX_EXPR_SOURCE_TOKENS) {free(text);return error(p,"For range token limit");}
            if (tokens>1 && !(write[-1]=='-' && (kind==ZSS_INT || kind==ZSS_FLOAT))) *write++=' ';
            memcpy(write,p->lex.source+p->lex.token.offset,p->lex.token.length);write+=p->lex.token.length;
            next(p);
        }
        end=text;
        if (!tokens || !Expr_CompileCtx(text,&p->program->nodes[index].range[part],p->owner,&end,EXPR_WANT_ANY,&p->context) || *end) {
            free(text);return error(p,"For range rejected by MUGEN compiler");
        }
        free(text);
        if (part==0 && kind!=';') return error(p,"For range needs begin and end expressions");
        if (part==1 && kind=='{') {Expr_SetInt(&p->program->nodes[index].range[2],1);break;}
        if (part==2 && kind!='{') return error(p,"Expected '{' after for increment");
        if (part<2) next(p);
    }
    /* The third expression leaves '{' current; the two-expression form does too. */
    next(p);
    if (!body(p,depth+1,loops+1,0,&child)) return 0;
    if (p->lex.token.kind!='}') return error(p,"Expected '}' after for body");
    p->program->nodes[index].child=child;
    *id=index;*end_line=p->lex.token.line;next(p);return 1;
}
/* Port addition: retain the reference's textual header = value || ... guard
 * construction. In particular, do not hoist a side-effecting header or add
 * parentheses that change its precedence. Buffers own only compiler scratch. */
static int switch_expression(ZssParser *p, int close1, int close2, char **out, unsigned *tokens)
{
    ZssLexer scan=p->lex;
    size_t bytes=0;
    unsigned count=0, depth=0, i;
    int kind;
    char *text,*write;
    for (;;) {
        kind=scan.token.kind;
        if (!depth && (kind==close1 || kind==close2)) break;
        if (kind<=0 || kind=='{' || kind=='}' || kind==';' || kind==':') {
            p->lex=scan;return error(p,"Incomplete switch expression or delimiter");
        }
        if (kind=='(' || kind=='[') depth++;
        if (kind==')' || kind==']') {
            if (!depth) {p->lex=scan;return error(p,"Unmatched switch expression delimiter");}
            depth--;
        }
        if (++count>=MAX_EXPR_SOURCE_TOKENS || depth>MAX_DEPTH) {
            p->lex=scan;return error(p,"Switch expression token/nesting limit");
        }
        bytes+=scan.token.length;
        ZssLex_Next(&scan);
    }
    if (!count) return error(p,"Empty switch header or case value");
    text=calloc(1,bytes+count+1);
    if (text==NULL) return error(p,"Out of memory reading switch expression");
    write=text;
    for (i=0;i<count;i++) {
        kind=p->lex.token.kind;
        if (i && !(write[-1]=='-' && (kind==ZSS_INT || kind==ZSS_FLOAT))) *write++=' ';
        memcpy(write,p->lex.source+p->lex.token.offset,p->lex.token.length);write+=p->lex.token.length;
        next(p);
    }
    *out=text;*tokens=count;return 1;
}
static int switch_guard_append(ZssParser *p, char **guard, size_t *length,
                                const char *header, const char *value, int more)
{
    size_t h=strlen(header),v=strlen(value),extra=h+v+3+(more?4:0);
    size_t limit=2u*ZSS_MAX_SOURCE-p->switch_bytes;
    char *text;
    if (*length>limit || extra>limit-*length) return error(p,"Switch guard expansion budget (2 MiB)");
    text=calloc(1,*length+extra+1);
    if (text==NULL) return error(p,"Out of memory constructing switch guard");
    if (*length) memcpy(text,*guard,*length);
    if (more) memcpy(text+*length," || ",4);
    memcpy(text+*length+(more?4:0),header,h);
    memcpy(text+*length+(more?4:0)+h," = ",3);
    memcpy(text+*length+(more?4:0)+h+3,value,v);
    free(*guard);*guard=text;*length+=extra;return 1;
}
static int switch_block(ZssParser *p, unsigned depth, unsigned loops, uint32_t *id, uint32_t *end_line)
{
    uint32_t root=node(p),first=NO_NODE,last=NO_NODE,fallback=NO_NODE,entry,child;
    unsigned header_tokens,value_tokens,guard_tokens;
    char *header=NULL,*value=NULL,*guard=NULL;
    const char *end;
    size_t length;
    int seen_default=0,labels=0,more,kind;
    if (root==NO_NODE) return 0;
    if (depth>=MAX_DEPTH) return error(p,"ZSS block nesting limit");
    p->program->nodes[root].kind=NODE_IF;Expr_SetInt(&p->program->nodes[root].condition,1);
    next(p);
    if (!switch_expression(p,'{','{',&header,&header_tokens)) goto fail;
    next(p);
    while (p->lex.token.kind!='}') {
        labels++;
        if (ZssLex_Is(&p->lex,"default")) {
            if (seen_default) {error(p,"Duplicate switch default");goto fail;}
            seen_default=1;
            if (next(p)!=':') {error(p,"Expected ':' after default");goto fail;}
            next(p);
            if (!body(p,depth+1,loops,1,&fallback)) goto fail;
            continue;
        }
        if (!ZssLex_Is(&p->lex,"case")) {error(p,"Expected case/default or closing switch brace");goto fail;}
        entry=node(p);if (entry==NO_NODE) goto fail;
        p->program->nodes[entry].kind=NODE_CASE;
        next(p);length=0;guard_tokens=0;more=0;
        for (;;) {
            if (!switch_expression(p,';',':',&value,&value_tokens)) goto fail;
            guard_tokens+=header_tokens+1+value_tokens+(more?1:0);
            if (guard_tokens>=EXPR_MAX_TOKENS) {error(p,"Combined switch guard token limit");goto fail;}
            if (!switch_guard_append(p,&guard,&length,header,value,more)) goto fail;
            free(value);value=NULL;
            kind=p->lex.token.kind;
            if (kind==':') break;
            more=1;next(p);
        }
        end=guard;
        if (!Expr_CompileCtx(guard,&p->program->nodes[entry].condition,p->owner,&end,EXPR_WANT_ANY,&p->context) || *end) {
            error(p,"Switch guard rejected by MUGEN expression compiler");goto fail;
        }
        p->switch_bytes+=length;free(guard);guard=NULL;
        next(p);
        if (!body(p,depth+1,loops,1,&child)) goto fail;
        p->program->nodes[entry].child=child;
        if (last==NO_NODE) first=entry;
        else p->program->nodes[last].otherwise=entry;
        last=entry;
    }
    if (!labels) {error(p,"Switch needs a case or default");goto fail;}
    if (last!=NO_NODE) p->program->nodes[last].otherwise=fallback;
    p->program->nodes[root].child=first!=NO_NODE ? first : fallback;
    *id=root;*end_line=p->lex.token.line;next(p);free(header);return 1;
fail:
    free(header);free(value);free(guard);return 0;
}

static int body(ZssParser *p, unsigned depth, unsigned loops, int labels, uint32_t *first)
{
    uint32_t last=NO_NODE, id, end_line;
    ZssToken start;
    int kind, persistent;
    unsigned inherited, attr_ihp;
    *first=NO_NODE;
    for (;;) {
        kind=p->lex.token.kind;
        if (kind==ZSS_EOF || kind=='}' || kind=='[') return 1;
        if (labels && (ZssLex_Is(&p->lex,"case") || ZssLex_Is(&p->lex,"default"))) return 1;
        inherited=p->ihp;attr_ihp=inherited;persistent=-1;
        while (ZssLex_Is(&p->lex,"ignorehitpause") || ZssLex_Is(&p->lex,"persistent")) {
            if (ZssLex_Is(&p->lex,"ignorehitpause")) {attr_ihp=1;next(p);}
            else {
                char value[32];
                if (persistent!=-1 || p->in_function || p->state_number<0) return error(p,"Invalid persistent block scope");
                if (next(p)!='(' || next(p)!=ZSS_INT || p->lex.token.length>=sizeof(value)) return error(p,"Expected persistent integer");
                memcpy(value,p->lex.source+p->lex.token.offset,p->lex.token.length);value[p->lex.token.length]=0;
                persistent=Expr_Atoi32(value);
                if (persistent<0 || persistent>255 || next(p)!=')') return error(p,"Persistent count must be 0..255");
                next(p);
            }
        }
        p->ihp=attr_ihp;kind=p->lex.token.kind;
        if (kind=='{') {
            uint32_t child;
            if (depth>=MAX_DEPTH) return error(p,"ZSS block nesting limit");
            id=node(p);if (id==NO_NODE) return 0;
            p->program->nodes[id].kind=NODE_IF;Expr_SetInt(&p->program->nodes[id].condition,1);
            next(p);
            if (!body(p,depth+1,loops,0,&child)) return 0;
            if (p->lex.token.kind!='}') return error(p,"Expected closing block brace");
            p->program->nodes[id].child=child;end_line=p->lex.token.line;next(p);
        } else if (ZssLex_Is(&p->lex,"switch")) {
            if (!switch_block(p,depth,loops,&id,&end_line)) return 0;
        } else if (ZssLex_Is(&p->lex,"if") || ZssLex_Is(&p->lex,"while")) {
            if (!branch(p,depth,loops,ZssLex_Is(&p->lex,"while"),&id,&end_line)) return 0;
        } else if (ZssLex_Is(&p->lex,"let")) {
            int locals[256], local;
            unsigned count=0, k;
            do {
                next(p);local=declare_local(p);if (local==-1) return 0;
                if (count==256) return error(p,"Call result limit (256)");
                for (k=0;k<count;k++) if (local>=0 && locals[k]==local)
                    return error(p,"Duplicate let binding");
                locals[count++]=local;
            } while (next(p)==',');
            if (p->lex.token.kind!='=') return error(p,"Expected '=' after let names");
            next(p);
            if (ZssLex_Is(&p->lex,"call")) {
                if (!call_statement(p,locals,count,&id,&end_line)) return 0;
            } else {
                if (count!=1) return error(p,"Multiple numeric let values are not enabled; bind call results instead");
                if (!assignment(p,locals[0],&id,&end_line)) return 0;
            }
        } else if (ZssLex_Is(&p->lex,"call")) {
            if (!call_statement(p,NULL,0,&id,&end_line)) return 0;
        } else if (ZssLex_Is(&p->lex,"for")) {
            if (!range_loop(p,depth,loops,&id,&end_line)) return 0;
        } else if (ZssLex_Is(&p->lex,"break") || ZssLex_Is(&p->lex,"continue")) {
            if (!loops) return error(p,"Break/continue can only appear inside a loop");
            id=node(p);if (id==NO_NODE) return 0;
            p->program->nodes[id].kind=ZssLex_Is(&p->lex,"break") ? NODE_BREAK : NODE_CONTINUE;
            if (next(p)!=';') return error(p,"Expected ';' after break/continue");
            end_line=p->lex.token.line;next(p);
        } else {
            ZssLexer peek=p->lex;
            if (kind!=ZSS_IDENT || ZssLex_Next(&peek)!='{') {
                if (!assignment(p,-1,&id,&end_line)) return 0;
                goto linked;
            }
            start=p->lex.token;
            next(p);
            if (!enclosed(p,0)) return 0;
            id=node(p);if (id==NO_NODE) return 0;
            if (!Zss_CompileControllerCtx(p->lex.source+start.offset,p->lex.pos-start.offset,
                                      p->owner,&p->program->nodes[id].controller,p->diagnostic,&p->context)) {
                relocate(p->diagnostic,&start);return 0;
            }
            end_line=p->lex.token.line;next(p);
        }
linked:
        p->program->nodes[id].ihp=attr_ihp;
        if (persistent!=-1) {
            if (p->program->nodes[id].kind!=NODE_IF) {
                uint32_t wrapper;
                /* Nonconditional leaves use the same persistence gate as an
                 * always-true block. Loop attributes need separate semantics. */
                if(p->program->nodes[id].kind==NODE_WHILE || p->program->nodes[id].kind==NODE_FOR ||
                   p->program->nodes[id].kind==NODE_CASE) return error(p,"Persistent loop/switch attribute is not enabled");
                wrapper=node(p);if(wrapper==NO_NODE) return 0;
                p->program->nodes[wrapper].kind=NODE_IF;
                Expr_SetInt(&p->program->nodes[wrapper].condition,1);
                p->program->nodes[wrapper].child=id;p->program->nodes[wrapper].ihp=attr_ihp;id=wrapper;
            }
            if (p->persistence_count>=500) return error(p,"Persistent block limit (500)");
            p->program->nodes[id].persistent_slot=++p->persistence_count;
            p->program->nodes[id].persistence=persistent;
        }
        p->ihp=inherited;
        if (*first==NO_NODE) *first=id;
        if (last!=NO_NODE) p->program->nodes[last].next=id;
        last=id;
        if (depth==0 && p->lex.token.kind!=ZSS_EOF && p->lex.token.line==end_line)
            return error(p,"A root statement must end on its own line");
    }
}
static int function_header(ZssParser *p, uint32_t *index)
{
    ZssFunction *functions, *function;
    char name[64];
    uint32_t i;
    unsigned part;
    ZssToken token;
    next(p); /* Function */
    next(p); /* name */
    if (!copy_name(p,name)) return 0;
    for (i=0;i<p->program->function_count;i++) if (!strcmp(name,p->program->functions[i].name))
        return error(p,"Duplicate ZSS function in the same file");
    functions=grow(p->program->functions,&p->program->function_capacity,p->program->function_count,
                   sizeof(*functions),MAX_FUNCTIONS);
    if (functions==NULL) return error(p,"ZSS function limit (64) or allocation failure");
    p->program->functions=functions;*index=p->program->function_count++;
    function=&functions[*index];strcpy(function->name,name);function->first=NO_NODE;
    if (next(p)!='(') return error(p,"Expected '(' after function name");
    p->local_count=0;next(p);
    for (part=0;part<2;part++) {
        while (p->lex.token.kind!=(part ? ']' : ')')) {
            token=p->lex.token;
            if (!valid_name(p,&token)) return 0;
            if (part && token.length==1 && p->lex.source[token.offset]=='_')
                return error(p,"Function return name cannot be '_'");
            if (!(token.length==1 && p->lex.source[token.offset]=='_') &&
                resolve_local(p,p->lex.source+token.offset,token.length)>=0)
                return error(p,"Duplicate function argument/return name");
            if (p->local_count>=256) return error(p,"ZSS local variable limit (256)");
            p->locals[p->local_count++]=token;
            if (part) function->returns++;else function->arguments++;
            if (next(p)!=',') break;
            if (next(p)==(part ? ']' : ')')) return error(p,"Missing function parameter after ','");
        }
        if (p->lex.token.kind!=(part ? ']' : ')')) return error(p,"Invalid function argument/return list");
        if (!part) next(p);
    }
    return 1;
}
static unsigned sequence_ihp(ZssProgram *p, uint32_t at)
{
    unsigned any=0;
    while (at!=NO_NODE) {
        ZssNode *n=&p->nodes[at];
        n->has_ihp=n->ihp | sequence_ihp(p,n->child) | sequence_ihp(p,n->otherwise);
        any|=n->has_ihp;at=n->next;
    }
    return any;
}
int Zss_CompileProgram(const char *source, size_t size, CharData *owner,
                       ZssProgram **out, ZssDiagnostic *diagnostic)
{
    ZssParser p={0};
    ZssToken start;
    ZssState state={0};
    uint32_t i, first, header_end;
    ZssState *states;
    p.owner=owner;p.diagnostic=diagnostic;
    p.context.user=&p;p.context.resolve_local=resolve_local;
    if (diagnostic!=NULL) memset(diagnostic,0,sizeof(*diagnostic));
    p.lex.token.line=p.lex.token.column=1;
    if (source==NULL || owner==NULL || out==NULL || size>ZSS_MAX_SOURCE)
        return error(&p,"Invalid ZSS program source");
    if (!ZssLex_Init(&p.lex,source,size,65536)) return error(&p,p.lex.error);
    p.program=calloc(1,sizeof(*p.program));
    if (p.program==NULL) return error(&p,"Out of memory creating ZSS program");
    p.program->references=1;
    next(&p);
    while (p.lex.token.kind!=ZSS_EOF) {
        ZssLexer peek=p.lex;
        start=p.lex.token;
        if (start.kind!='[') {error(&p,"Expected StateDef header");goto fail;}
        ZssLex_Next(&peek);
        if (ZssLex_Is(&peek,"function")) {
            p.in_function=1;p.ihp=1;p.persistence_count=0;
            if (!function_header(&p,&i)) goto fail;
            header_end=p.lex.token.line;next(&p);
            if (p.lex.token.kind!=ZSS_EOF && p.lex.token.line==header_end) {
                error(&p,"Function header must end on its own line");goto fail;
            }
            if (!body(&p,0,0,0,&first)) goto fail;
            p.program->functions[i].first=first;p.program->functions[i].locals=p.local_count;
            continue;
        }
        if (!enclosed(&p,1)) goto fail;
        if (!Zss_CompileStateHeader(source+start.offset,p.lex.pos-start.offset,owner,
                                    &state.number,&state.properties,diagnostic)) {
            relocate(diagnostic,&start);goto fail;
        }
        for (i=0;i<p.program->state_count;i++) if (p.program->states[i].number==state.number) {
            error(&p,"Duplicate ZSS state number");goto fail;
        }
        states=grow(p.program->states,&p.program->state_capacity,p.program->state_count,
                    sizeof(*p.program->states),MAX_STATES);
        if (states==NULL) {error(&p,"ZSS state limit or allocation failure");goto fail;}
        p.program->states=states;
        i=p.program->state_count++;p.program->states[i]=state;memset(&state,0,sizeof(state));
        p.program->states[i].first=NO_NODE;
        header_end=p.lex.token.line;next(&p);
        if (p.lex.token.kind!=ZSS_EOF && p.lex.token.line==header_end) {
            error(&p,"StateDef header must end on its own line");goto fail;
        }
        p.local_count=0;p.in_function=0;p.ihp=0;p.persistence_count=0;p.state_number=state.number;
        if (!body(&p,0,0,0,&first)) goto fail;
        p.program->states[i].first=first;p.program->states[i].locals=p.local_count;
    }
    if (!p.program->state_count && !p.program->function_count) {error(&p,"Program needs a StateDef or Function");goto fail;}
    for(i=0;i<p.program->state_count;i++) sequence_ihp(p.program,p.program->states[i].first);
    for(i=0;i<p.program->function_count;i++) sequence_ihp(p.program,p.program->functions[i].first);
    *out=p.program;return 1;
fail:
    StateDef_FreeProperties(&state.properties);Zss_FreeProgram(p.program);return 0;
}
void Zss_FreeProgram(ZssProgram *p)
{
    uint32_t i;
    if (p==NULL || --p->references!=0) return;
    for (i=0;i<p->node_count;i++) {
        unsigned j;
        Expr_Free(&p->nodes[i].condition);
        for (j=0;j<3;j++) Expr_Free(&p->nodes[i].range[j]);
        StateCtrl_Free(&p->nodes[i].controller);
        if (p->nodes[i].call!=NULL) {
            ZssCall *call=p->nodes[i].call;
            for (j=0;j<call->argument_count;j++) Expr_Free(&call->arguments[j]);
            free(call->arguments);free(call->returns);free(call);
        }
    }
    for (i=0;i<p->state_count;i++) StateDef_FreeProperties(&p->states[i].properties);
    free(p->nodes);free(p->states);free(p->functions);free(p);
}
size_t Zss_ProgramStateCount(const ZssProgram *p) {return p!=NULL?p->state_count:0;}
const StateDef *Zss_ProgramState(const ZssProgram *p, size_t index, int *number)
{
    if (p==NULL || index>=p->state_count) return NULL;
    if (number!=NULL) *number=p->states[index].number;
    return &p->states[index].properties;
}
/* Preserve MUGEN numeric conversion and bottom-as-zero without a fatal
 * evaluator path if a malformed expression somehow reaches the prototype. */
static int local_integer(ExprEvalCtx *ctx, int slot)
{
    return ctx->types[slot]==EXPR_INT ? ctx->values[slot].i :
           ctx->types[slot]==EXPR_FLOAT ? Ftol((double)ctx->values[slot].f) : 0;
}
static int eval_value(Player *player, const Expr *e, ExprEvalCtx *ctx, int local, int32_t *value)
{
    int i=0, type;
    float f=0;
    type=Expr_EvalCtx(player,e,&i,&f,ctx);
    if (!type) return 0;
    if (local>=0) {
        ctx->types[local]=type==1 ? EXPR_INT : type==2 ? EXPR_FLOAT : EXPR_BOTTOM;
        ctx->values[local].i=0;
        if (type==1) ctx->values[local].i=i;
        else if (type==2) ctx->values[local].f=f;
    }
    if (value!=NULL) *value=type==1 ? i : type==2 ? Ftol((double)f) : 0;
    return 1;
}
static const ZssFunction *find_function(ZssProgram *p, const ZssLibrary *library,
                                       const char *name, ZssProgram **module)
{
    uint32_t i, j, count=library!=NULL ? library->count : 1;
    for (i=0;i<count;i++) {
        ZssProgram *candidate=library!=NULL ? library->modules[i] : p;
        for (j=0;j<candidate->function_count;j++) if (!strcmp(name,candidate->functions[j].name)) {
            *module=candidate;return &candidate->functions[j];
        }
    }
    return NULL;
}
static void call_warning(Player *player, unsigned *warnings, const char *name, const char *reason)
{
    char message[192];
    if ((*warnings)++>=4) return;
    snprintf(message,sizeof(message),"ZSS function %s: %s",name,reason);Player_Warn(player,message);
}
static int run_body(ZssProgram *p, size_t state, Player *player,
                    ZssControllerRun run, void *context, int *result, const ZssLibrary *library)
{
    /* A sequence frame resumes after a child block. Loop frames own iteration
     * counts, so shared programs and reentrant runs never share loop cursors. */
    struct Frame {uint32_t current, iterations;int loop;int32_t value, end, increment;} frames[MAX_FRAMES];
    struct Activation {
        ZssProgram *program;
        ExprEvalCtx locals;
        const ZssCall *destination;
        uint32_t base, arguments;
        void *storage;
    } activations[MAX_CALL_DEPTH+1];
    uint32_t top=1, steps=0, count, chosen;
    uint32_t active_count=1;
    unsigned call_warnings=0;
    int bits=0, i=0, type, selected, warned=0, ok=0;
    float f=0;
    ZssNode *n;
    ExprValue local_values[256]={{0}};
    char local_types[256];
    ExprEvalCtx rootctx={local_values,local_types,0}, *evalctx;
    if (p==NULL || state>=p->state_count || player==NULL || run==NULL || result==NULL) return 0;
    rootctx.count=p->states[state].locals;
    memset(local_types,EXPR_BOTTOM,sizeof(local_types));
    activations[0]=(struct Activation){p,rootctx,NULL,0,0,NULL};
    frames[0]=(struct Frame){p->states[state].first,0,0,0,0,0};
    while (active_count) {
        struct Activation *active=&activations[active_count-1];
        struct Frame *frame;
        p=active->program;evalctx=&active->locals;
        if (++steps>MAX_STEPS) {
            Player_Warn(player,"ZSS execution stopped at total step budget");
            bits|=STATECTRL_STOP;ok=1;goto cleanup;
        }
        if (top==active->base) {
            if (active_count==1) {active_count=0;break;}
            for (count=0;count<active->destination->return_count;count++) {
                int slot=active->destination->returns[count];
                ExprEvalCtx *caller=&activations[active_count-2].locals;
                if (slot>=0) {
                    caller->values[slot]=evalctx->values[active->arguments+count];
                    caller->types[slot]=evalctx->types[active->arguments+count];
                }
            }
            free(active->storage);active_count--;continue;
        }
        frame=&frames[top-1];
        if (frame->current==NO_NODE) {top--;continue;}
        n=&p->nodes[frame->current];
        if (frame->loop) {
            if (n->kind==NODE_FOR) {
                if (frame->iterations) {
                    /* Go int32 range arithmetic wraps. Do it in int64 first,
                     * avoiding signed-overflow UB and implementation-defined casts. */
                    int64_t value=(int64_t)(n->local>=0 ? local_integer(evalctx,n->local) : frame->value)+frame->increment;
                    if (value>INT32_MAX) value-=INT64_C(4294967296);
                    if (value<INT32_MIN) value+=INT64_C(4294967296);
                    frame->value=(int32_t)value;
                }
                selected=frame->increment>0 ? frame->value<=frame->end :
                         frame->increment<0 ? frame->value>=frame->end : 1;
            } else {
                if (frame->iterations>=MAX_LOOP) {
                    if (!warned++) Player_Warn(player,"ZSS loop stopped after 2500 iterations");
                    top--;continue;
                }
                type=Expr_EvalCtx(player,&n->condition,&i,&f,evalctx);
                if (type==0) goto cleanup;
                selected=type==1 ? i!=0 : type==2 ? (!_isnan(f) && f!=0) : 0;
            }
            if (!selected) {top--;continue;}
            if (n->kind==NODE_FOR && n->local>=0 && frame->iterations) {
                evalctx->values[n->local].i=frame->value;evalctx->types[n->local]=EXPR_INT;
            }
            if (frame->iterations>=MAX_LOOP) {
                if (!warned++) Player_Warn(player,"ZSS loop stopped after 2500 iterations");
                top--;continue;
            }
            frame->iterations++;
            if (top>=MAX_FRAMES) goto frame_limit;
            frames[top++]=(struct Frame){n->child,0,0,0,0,0};continue;
        }
        frame->current=n->next;
        if (player->hitpause_time>0 && !n->has_ihp) continue;
        if (n->persistent_slot) {
            unsigned char *timer=&player->sctrl_wait[n->persistent_slot];
            if (n->persistence==0 && *timer) continue;
            if (n->persistence>0 && *timer) {--*timer;if (*timer) continue;}
        }
        switch (n->kind) {
        case NODE_CASE:
            type=Expr_EvalCtx(player,&n->condition,&i,&f,evalctx);
            if (!type) goto cleanup;
            selected=type==1 ? i!=0 : type==2 ? (!_isnan(f) && f!=0) : 0;
            /* A case chain tail-dispatches without stacking one empty frame
             * per failed label. A match (even empty) never falls through. */
            frame->current=selected ? n->child : n->otherwise;
            break;
        case NODE_IF:
            type=Expr_EvalCtx(player,&n->condition,&i,&f,evalctx);
            if (type==0) goto cleanup;
            selected=type==1 ? i!=0 : type==2 ? (!_isnan(f) && f!=0) : 0;
            chosen=selected?n->child:n->otherwise;
            if (selected && n->persistent_slot) player->sctrl_wait[n->persistent_slot]=(unsigned char)(n->persistence==0 ? 1 : n->persistence);
            if (chosen!=NO_NODE) {
                if (top>=MAX_FRAMES) goto frame_limit;
                frames[top++]=(struct Frame){chosen,0,0,0,0,0};
            }
            break;
        case NODE_FOR:
            if (top>=MAX_FRAMES) goto frame_limit;
            frames[top]=(struct Frame){(uint32_t)(n-p->nodes),0,1,0,0,0};
            if (!eval_value(player,&n->range[0],evalctx,n->local,&frames[top].value) ||
                !eval_value(player,&n->range[1],evalctx,-1,&frames[top].end) ||
                !eval_value(player,&n->range[2],evalctx,-1,&frames[top].increment)) goto cleanup;
            top++;break;
        case NODE_WHILE:
            if (top>=MAX_FRAMES) goto frame_limit;
            frames[top++]=(struct Frame){(uint32_t)(n-p->nodes),0,1,0,0,0};break;
        case NODE_LET:
            if (!eval_value(player,&n->condition,evalctx,n->local,NULL)) goto cleanup;
            break;
        case NODE_EXPRESSION:
            if (!Expr_EvalCtx(player,&n->condition,&i,&f,evalctx)) goto cleanup;
            break;
        case NODE_CALL:
        {
            const ZssCall *call=n->call;
            ZssProgram *module=NULL;
            const ZssFunction *function=find_function(p,library,call->name,&module);
            struct Activation *callee;
            void *storage=NULL;
            if (function==NULL || call->argument_count!=function->arguments ||
                (call->return_count && call->return_count!=function->returns)) {
                call_warning(player,&call_warnings,call->name,function==NULL ? "undefined function" :
                    call->argument_count!=function->arguments ? "argument count mismatch" : "return count mismatch");
                break; /* Validate before evaluating any argument side effects. */
            }
            if (active_count==MAX_CALL_DEPTH+1) {
                call_warning(player,&call_warnings,call->name,"call depth limit (32)");
                bits|=STATECTRL_STOP;ok=1;goto cleanup;
            }
            if (top>=MAX_FRAMES) goto frame_limit;
            if (function->locals) {
                storage=calloc(function->locals,sizeof(ExprValue)+sizeof(char));
                if (storage==NULL) {
                    call_warning(player,&call_warnings,call->name,"out of memory creating local frame");
                    bits|=STATECTRL_STOP;ok=1;goto cleanup;
                }
            }
            callee=&activations[active_count++];
            *callee=(struct Activation){module,{storage,storage!=NULL ?
                (char *)storage+function->locals*sizeof(ExprValue) : NULL,function->locals},
                call,top,function->arguments,storage};
            if (function->locals) memset(callee->locals.types,EXPR_BOTTOM,function->locals);
            for (count=0;count<call->argument_count;count++) {
                type=Expr_EvalCtx(player,&call->arguments[count],&i,&f,evalctx);
                if (!type) goto cleanup;
                callee->locals.types[count]=type==1 ? EXPR_INT : type==2 ? EXPR_FLOAT : EXPR_BOTTOM;
                if (type==1) callee->locals.values[count].i=i;
                else if (type==2) callee->locals.values[count].f=f;
            }
            frames[top++]=(struct Frame){function->first,0,0,0,0,0};break;
        }
        case NODE_BREAK:
        case NODE_CONTINUE:
            count=top;
            while (count>active->base && !frames[count-1].loop) count--;
            if (count==active->base) goto cleanup;
            top=n->kind==NODE_BREAK ? count-1 : count;break;
        default:
            bits|=run(player,&n->controller,evalctx,context);
            if (bits&STATECTRL_STOP) {ok=1;goto cleanup;}
            break;
        }
    }
    ok=1;goto cleanup;
frame_limit:
    Player_Warn(player,"ZSS execution frame limit (256)");bits|=STATECTRL_STOP;ok=1;
cleanup:
    while (active_count>1) free(activations[--active_count].storage);
    if (ok) *result=bits;
    return ok;
}
int Zss_RunBody(ZssProgram *p, size_t state, Player *player,
                ZssControllerRun run, void *context, int *result)
{
    return run_body(p,state,player,run,context,result,NULL);
}

/* Port integration: a private controller owns one reference to an immutable
 * program. No global sidecar or pointer into a growing StateDef array is used. */
typedef struct ZssBinding {
    ZssProgram *program;
    ZssLibrary *library;
    size_t state;
    unsigned trace_budget;
} ZssBinding;
static void free_library(ZssLibrary *library)
{
    uint32_t i;
    if (library==NULL || --library->references) return;
    for (i=0;i<library->count;i++) Zss_FreeProgram(library->modules[i]);
    free(library->modules);free(library);
}
void Zss_FreeBoundBody(void *binding)
{
    ZssBinding *b=binding;
    if (b==NULL) return;
    Zss_FreeProgram(b->program);free_library(b->library);free(b);
}
static int run_bound_leaf(Player *p, StateCtrl *controller, ExprEvalCtx *frame, void *context)
{
    /* Keep command hints collected by this body's guards for the existing AI
     * bookkeeping in StateCtrl_Run. The outer state loop reset them already. */
    return StateCtrl_RunCtx(p,controller,context,frame);
}
int Zss_RunBoundBody(Player *p, void *binding, StateRunCtx *context)
{
    ZssBinding *b=binding;
    int result=0;
    if (b==NULL || !run_body(b->program,b->state,p,run_bound_leaf,context,&result,b->library)) {
        Player_Warn(p,"ZSS body execution failed");return STATECTRL_STOP;
    }
    /* Opt-in, bounded diagnostics for qualifying the experimental engine path. */
    if (b->trace_budget) {
        b->trace_budget--;
        Log_Printf("ZSS state %i body result %i\n",b->program->states[b->state].number,result);
    }
    return result;
}
int Zss_IsBoundState(const StateDef *sd)
{
    const StateCtrl *sc;
    int i;
    if (sd==NULL || sd->ctrls==NULL || sd->ctrls->count<1) return 0;
    for(i=0;i<sd->ctrls->capacity;i++) if(sd->ctrls->slots[i].used) {
        sc=(const StateCtrl *)(sd->ctrls->data+sd->ctrls->elem_size*i);
        if(sc->type!=SCTRL_ZSS_BODY || sc->params==NULL) return 0;
    }
    return 1;
}
void Zss_PrepareStateCounters(Player *p,const StateDef *sd,int force)
{
    int state=0,id=0;
    if(!Zss_IsBoundState(sd)) return;
    memcpy(&state,&p->sctrl_wait[504],sizeof(state));memcpy(&id,&p->sctrl_wait[508],sizeof(id));
    if(force || state!=p->state_no || id!=p->id) {
        memset(p->sctrl_wait,0,504);
        memcpy(&p->sctrl_wait[504],&p->state_no,sizeof(p->state_no));memcpy(&p->sctrl_wait[508],&p->id,sizeof(p->id));
    }
}
void Zss_RunGlobalState(Player *p)
{
    int slot;
    StateDef *sd;
    if(p==NULL || p->states==NULL || p->states->list==NULL) return;
    slot=KeyedArray_Find(p->states->list,-4,0);
    if(slot<0) return;
    sd=(StateDef *)(p->states->list->data+p->states->list->elem_size*slot);
    if(Zss_IsBoundState(sd)) Player_RunSpecialState(p,-4,RUNSTATE_OWN|RUNSTATE_ALWAYS|RUNSTATE_CUSTOM);
}
static void free_staged_state(StateDef *sd)
{
    int i;
    if (sd->ctrls!=NULL) {
        for (i=0;i<sd->ctrls->capacity;i++) if (sd->ctrls->slots[i].used)
            StateCtrl_Free((StateCtrl *)(sd->ctrls->data+sd->ctrls->elem_size*i));
        KeyedArray_Free(sd->ctrls);
    }
    /* Property expressions are borrowed from the retained program. */
}
static int stage_binding(StateDef *sd, ZssProgram *p, size_t state, ZssLibrary *library)
{
    StateCtrl sc={0};
    Expr always={0};
    ZssBinding *binding;
    const char *trace;
    sd->ctrls=KeyedArray_Create(1,sizeof(StateCtrl));
    if (sd->ctrls==NULL) return 0;
    sc.triggers=KeyedArray_Create(1,sizeof(Expr));
    if (sc.triggers==NULL) goto fail;
    Expr_SetInt(&always,1);
    if (KeyedArray_Add(sc.triggers,&always,0,0)==-1) goto fail;
    sc.type=SCTRL_ZSS_BODY;sc.trigger_groups=1;sc.persistent=1;sc.ignorehitpause=1;
    binding=calloc(1,sizeof(*binding));if (binding==NULL) goto fail;
    binding->program=p;binding->library=library;binding->state=state;
    trace=getenv("MUGEN_ZSS_TRACE");
    binding->trace_budget=trace!=NULL && !strcmp(trace,"1") ? 4u : 0u;
    if (p!=NULL) p->references++;
    if (library!=NULL) library->references++;
    sc.params=binding;
    if (KeyedArray_Add(sd->ctrls,&sc,SCTRL_ZSS_BODY,0)==-1) goto fail;
    return 1;
fail:
    StateCtrl_Free(&sc);return 0;
}
int Zss_InstallProgram(ZssProgram *p, StateDefs *defs, const char **error)
{
    StateDef *staged=NULL;
    StateDef metadata={0};
    KeyedArray *merged=NULL, *old;
    KeyedArray **combined=NULL, **retired=NULL;
    ZssLibrary *library=NULL;
    ZssProgram **modules=NULL;
    uint32_t i;
    int k, add_metadata=0, extra;
    const char *why="Invalid ZSS installation target";
    if (error!=NULL) *error=NULL;
    if (p==NULL || defs==NULL || defs->list==NULL || (!p->state_count && !p->function_count)) goto fail;
    old=defs->list;
    if (old->elem_size!=(int)sizeof(StateDef)) goto fail;
    if (old->count>INT_MAX-(int)p->state_count-1 || p->references>UINT_MAX-p->state_count-1) goto fail;
    why="Out of memory installing ZSS states";
    /* Original state lookups always use key2=0. A single private key2=INT_MIN
     * entry anchors the library without growing StateDefs or a global sidecar.
     * It is freed through the same binding lifecycle, never scheduled as a state. */
    k=KeyedArray_Find(old,0,INT_MIN);
    if (k>=0) {
        StateDef *sd=(StateDef *)(old->data+old->elem_size*k);
        ZssBinding *binding;
        if (!Zss_IsBoundState(sd)) goto fail;
        binding=((StateCtrl *)(sd->ctrls->data+sd->ctrls->elem_size*sd->ctrls->first))->params;
        if (binding->program!=NULL || binding->state!=NO_NODE || binding->library==NULL) goto fail;
        library=binding->library;
        if (library->references>UINT_MAX-p->state_count-2) {library=NULL;goto fail;}
        library->references++; /* temporary installation reference */
    } else if (p->function_count || p->call_count) {
        library=calloc(1,sizeof(*library));if (library==NULL) goto fail;
        library->references=1;add_metadata=1;
        if (!stage_binding(&metadata,NULL,NO_NODE,library)) goto fail;
    }
    if (p->function_count) {
        if (library->count>=MAX_MODULES) {why="ZSS function module limit (64)";goto fail;}
        modules=calloc(library->count+1,sizeof(*modules));if (modules==NULL) goto fail;
        if (library->count) memcpy(modules,library->modules,library->count*sizeof(*modules));
        modules[library->count]=p;
    }
    if (p->state_count) {
        staged=calloc(p->state_count,sizeof(*staged));if (staged==NULL) goto fail;
        combined=calloc(p->state_count,sizeof(*combined));retired=calloc(p->state_count,sizeof(*retired));
        if (combined==NULL || retired==NULL) goto fail;
    }
    for (i=0;i<p->state_count;i++) {
        /* Immutable properties stay owned by the program, whose body binding
         * retains it. StateDefs_Free skips these borrowed expressions. */
        int slot=KeyedArray_Find(old,p->states[i].number,0);
        StateDef *previous=slot<0 ? NULL : (StateDef *)(old->data+old->elem_size*slot);
        int j;
        if (previous!=NULL && p->states[i].number>=0) continue; /* first positive definition wins */
        staged[i]=p->states[i].properties;
        if (!stage_binding(&staged[i],p,i,library)) goto fail;
        if (previous!=NULL) {
            if (!Zss_IsBoundState(previous)) {why="Mixed CNS/ZSS negative-state composition is not enabled";goto fail;}
            combined[i]=KeyedArray_Create(previous->ctrls->count+1,sizeof(StateCtrl));
            if(combined[i]==NULL) goto fail;
            for(j=0;j<previous->ctrls->capacity;j++) if(previous->ctrls->slots[j].used &&
                KeyedArray_Add(combined[i],previous->ctrls->data+previous->ctrls->elem_size*j,j,0)<0) goto fail;
            if(KeyedArray_Add(combined[i],KeyedArray_First(staged[i].ctrls),previous->ctrls->capacity,0)<0) goto fail;
            retired[i]=previous->ctrls;
        }
    }
    extra=(int)p->state_count+add_metadata;
    merged=KeyedArray_Create(old->count+extra,sizeof(StateDef));
    if (merged==NULL) goto fail;
    for (k=0;k<old->capacity;k++) if (old->slots[k].used &&
        KeyedArray_Add(merged,old->data+old->elem_size*k,old->slots[k].key1,old->slots[k].key2)==-1) goto fail;
    for (i=0;i<p->state_count;i++) {
        int slot;
        StateDef entry;
        if (staged[i].ctrls==NULL) continue;
        entry=staged[i];
        if (combined[i]!=NULL) {
            slot=KeyedArray_Find(old,p->states[i].number,0);
            entry=*(StateDef *)(old->data+old->elem_size*slot);entry.ctrls=combined[i];
        }
        slot=KeyedArray_Find(merged,p->states[i].number,0);
        if(slot>=0) *(StateDef *)(merged->data+merged->elem_size*slot)=entry;
        else if(KeyedArray_Add(merged,&entry,p->states[i].number,0)<0) goto fail;
    }
    if (add_metadata && KeyedArray_Add(merged,&metadata,0,INT_MIN)==-1) goto fail;
    /* Publish only after every allocation succeeds. Existing state bindings
     * share the stable library handle and see later function-only files. */
    if (modules!=NULL) {
        free(library->modules);library->modules=modules;library->count++;p->references++;
    }
    for(i=0;i<p->state_count;i++) if(combined[i]!=NULL) {
        KeyedArray_Free(staged[i].ctrls);KeyedArray_Free(retired[i]);
    }
    defs->list=merged;KeyedArray_Free(old);free(staged);free(combined);free(retired);free_library(library);return 1;
fail:
    KeyedArray_Free(merged);
    if(combined!=NULL) {for(i=0;i<p->state_count;i++) KeyedArray_Free(combined[i]);free(combined);}
    free(retired);
    if (staged!=NULL) {for (i=0;i<p->state_count;i++) free_staged_state(&staged[i]);free(staged);}
    free_staged_state(&metadata);free(modules);free_library(library);
    if (error!=NULL) *error=why;
    return 0;
}
