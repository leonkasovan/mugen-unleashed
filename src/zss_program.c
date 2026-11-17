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
#define MAX_DEPTH 32u
#define MAX_LOOP 2500u
#define MAX_STEPS 100000u
enum {NODE_CONTROLLER, NODE_IF, NODE_WHILE, NODE_EXPRESSION, NODE_BREAK, NODE_CONTINUE, NODE_FOR, NODE_LET, NODE_CASE};

typedef struct ZssNode {
    int kind, local;
    uint32_t next, child, otherwise;
    Expr condition;
    Expr range[3];
    StateCtrl controller;
} ZssNode;
typedef struct ZssState {
    int number;
    uint32_t first, locals;
    StateDef properties;
} ZssState;
struct ZssProgram {
    unsigned references;
    ZssNode *nodes;
    ZssState *states;
    uint32_t node_count, node_capacity, state_count, state_capacity;
};
typedef struct ZssParser {
    ZssLexer lex;
    ZssProgram *program;
    CharData *owner;
    ZssDiagnostic *diagnostic;
    ExprCompileCtx context;
    ZssToken locals[256];
    unsigned local_count;
    size_t switch_bytes;
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
    for (i=0;i<p->local_count;i++) {
        const ZssToken *t=&p->locals[i];
        if (t->length!=length) continue;
        for (j=0;j<length;j++) if (tolower((unsigned char)name[j]) !=
            tolower((unsigned char)p->lex.source[t->offset+j])) break;
        if (j==length) return (int)i;
    }
    return -1;
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
    for (size_t i=0;i<t.length;i++) {
        unsigned char c=(unsigned char)p->lex.source[t.offset+i];
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' ||
              (i>0 && c>='0' && c<='9'))) {
            error(p,"Invalid local variable name");return -1;
        }
    }
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
        if (++tokens>=EXPR_OP_STACK_SIZE) {free(expression);return error(p,"Condition token limit");}
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
        if (++tokens>=EXPR_OP_STACK_SIZE) {free(text);return error(p,"Assignment token limit");}
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
            if (++tokens>=EXPR_OP_STACK_SIZE) {free(text);return error(p,"For range token limit");}
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
        if (++count>=EXPR_OP_STACK_SIZE || depth>MAX_DEPTH) {
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
    int kind;
    *first=NO_NODE;
    for (;;) {
        kind=p->lex.token.kind;
        if (kind==ZSS_EOF || kind=='}' || kind=='[') return 1;
        if (labels && (ZssLex_Is(&p->lex,"case") || ZssLex_Is(&p->lex,"default"))) return 1;
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
            int local;
            next(p);local=declare_local(p);if (local==-1) return 0;
            if (next(p)!='=') return error(p,"Expected '=' after one let name; multiple bindings are not enabled");
            next(p);
            if (!assignment(p,local,&id,&end_line)) return 0;
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
            if (ZssLex_Is(&p->lex,"call") ||
                ZssLex_Is(&p->lex,"persistent") || ZssLex_Is(&p->lex,"ignorehitpause"))
                return error(p,"This ZSS statement is not implemented yet");
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
        if (*first==NO_NODE) *first=id;
        if (last!=NO_NODE) p->program->nodes[last].next=id;
        last=id;
        if (depth==0 && p->lex.token.kind!=ZSS_EOF && p->lex.token.line==end_line)
            return error(p,"A root statement must end on its own line");
    }
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
        start=p.lex.token;
        if (start.kind!='[') {error(&p,"Expected StateDef header");goto fail;}
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
        p.local_count=0;
        if (!body(&p,0,0,0,&first)) goto fail;
        p.program->states[i].first=first;p.program->states[i].locals=p.local_count;
    }
    if (!p.program->state_count) {error(&p,"Program needs a StateDef");goto fail;}
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
    }
    for (i=0;i<p->state_count;i++) StateDef_FreeProperties(&p->states[i].properties);
    free(p->nodes);free(p->states);free(p);
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
int Zss_RunBody(ZssProgram *p, size_t state, Player *player,
                ZssControllerRun run, void *context, int *result)
{
    /* A sequence frame resumes after a child block. Loop frames own iteration
     * counts, so shared programs and reentrant runs never share loop cursors. */
    struct Frame {uint32_t current, iterations;int loop;int32_t value, end, increment;} frames[MAX_DEPTH*2+2];
    uint32_t top=1, steps=0, count, chosen;
    int bits=0, i=0, type, selected, warned=0;
    float f=0;
    ZssNode *n;
    ExprValue local_values[256]={{0}};
    char local_types[256];
    ExprEvalCtx evalctx={local_values,local_types,0};
    if (p==NULL || state>=p->state_count || player==NULL || run==NULL || result==NULL) return 0;
    evalctx.count=p->states[state].locals;
    memset(local_types,EXPR_BOTTOM,sizeof(local_types));
    frames[0]=(struct Frame){p->states[state].first,0,0,0,0,0};
    while (top) {
        struct Frame *frame=&frames[top-1];
        if (++steps>MAX_STEPS) {
            Player_Warn(player,"ZSS execution stopped at total step budget");
            *result=bits|STATECTRL_STOP;return 1;
        }
        if (frame->current==NO_NODE) {top--;continue;}
        n=&p->nodes[frame->current];
        if (frame->loop) {
            if (n->kind==NODE_FOR) {
                if (frame->iterations) {
                    /* Go int32 range arithmetic wraps. Do it in int64 first,
                     * avoiding signed-overflow UB and implementation-defined casts. */
                    int64_t value=(int64_t)(n->local>=0 ? local_integer(&evalctx,n->local) : frame->value)+frame->increment;
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
                type=Expr_EvalCtx(player,&n->condition,&i,&f,&evalctx);
                if (type==0) return 0;
                selected=type==1 ? i!=0 : type==2 ? (!_isnan(f) && f!=0) : 0;
            }
            if (!selected) {top--;continue;}
            if (n->kind==NODE_FOR && n->local>=0 && frame->iterations) {
                local_values[n->local].i=frame->value;local_types[n->local]=EXPR_INT;
            }
            if (frame->iterations>=MAX_LOOP) {
                if (!warned++) Player_Warn(player,"ZSS loop stopped after 2500 iterations");
                top--;continue;
            }
            frame->iterations++;
            if (top>=MAX_DEPTH*2+2) return 0;
            frames[top++]=(struct Frame){n->child,0,0,0,0,0};continue;
        }
        frame->current=n->next;
        switch (n->kind) {
        case NODE_CASE:
            type=Expr_EvalCtx(player,&n->condition,&i,&f,&evalctx);
            if (!type) return 0;
            selected=type==1 ? i!=0 : type==2 ? (!_isnan(f) && f!=0) : 0;
            /* A case chain tail-dispatches without stacking one empty frame
             * per failed label. A match (even empty) never falls through. */
            frame->current=selected ? n->child : n->otherwise;
            break;
        case NODE_IF:
            type=Expr_EvalCtx(player,&n->condition,&i,&f,&evalctx);
            if (type==0) return 0;
            selected=type==1 ? i!=0 : type==2 ? (!_isnan(f) && f!=0) : 0;
            chosen=selected?n->child:n->otherwise;
            if (chosen!=NO_NODE) {
                if (top>=MAX_DEPTH*2+2) return 0;
                frames[top++]=(struct Frame){chosen,0,0,0,0,0};
            }
            break;
        case NODE_FOR:
            if (top>=MAX_DEPTH*2+2) return 0;
            frames[top]=(struct Frame){(uint32_t)(n-p->nodes),0,1,0,0,0};
            if (!eval_value(player,&n->range[0],&evalctx,n->local,&frames[top].value) ||
                !eval_value(player,&n->range[1],&evalctx,-1,&frames[top].end) ||
                !eval_value(player,&n->range[2],&evalctx,-1,&frames[top].increment)) return 0;
            top++;break;
        case NODE_WHILE:
            if (top>=MAX_DEPTH*2+2) return 0;
            frames[top++]=(struct Frame){(uint32_t)(n-p->nodes),0,1,0,0,0};break;
        case NODE_LET:
            if (!eval_value(player,&n->condition,&evalctx,n->local,NULL)) return 0;
            break;
        case NODE_EXPRESSION:
            if (!Expr_EvalCtx(player,&n->condition,&i,&f,&evalctx)) return 0;
            break;
        case NODE_BREAK:
        case NODE_CONTINUE:
            count=top;
            while (count && !frames[count-1].loop) count--;
            if (!count) return 0;
            top=n->kind==NODE_BREAK ? count-1 : count;break;
        default:
            bits|=run(player,&n->controller,&evalctx,context);
            if (bits&STATECTRL_STOP) {*result=bits;return 1;}
            break;
        }
    }
    *result=bits;return 1;
}

/* Port integration: a private controller owns one reference to an immutable
 * program. No global sidecar or pointer into a growing StateDef array is used. */
typedef struct ZssBinding {
    ZssProgram *program;
    size_t state;
    unsigned trace_budget;
} ZssBinding;
void Zss_FreeBoundBody(void *binding)
{
    ZssBinding *b=binding;
    if (b==NULL) return;
    Zss_FreeProgram(b->program);free(b);
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
    if (b==NULL || !Zss_RunBody(b->program,b->state,p,run_bound_leaf,context,&result)) {
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
    if (sd==NULL || sd->ctrls==NULL || sd->ctrls->count!=1) return 0;
    sc=(const StateCtrl *)(sd->ctrls->data+sd->ctrls->elem_size*sd->ctrls->first);
    return sc->type==SCTRL_ZSS_BODY && sc->params!=NULL;
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
int Zss_InstallProgram(ZssProgram *p, StateDefs *defs, const char **error)
{
    StateDef *staged=NULL;
    KeyedArray *merged=NULL, *old;
    StateCtrl sc={0};
    ZssBinding *binding;
    Expr always={0};
    uint32_t i;
    int k;
    const char *why="Invalid ZSS installation target";
    if (error!=NULL) *error=NULL;
    if (p==NULL || defs==NULL || defs->list==NULL || !p->state_count) goto fail;
    old=defs->list;
    if (old->elem_size!=(int)sizeof(StateDef)) goto fail;
    if (old->count>INT_MAX-(int)p->state_count || p->references>UINT_MAX-p->state_count) goto fail;
    for (i=0;i<p->state_count;i++) if (KeyedArray_Find(old,p->states[i].number,0)!=-1) {
        why="ZSS state collides with an already loaded state";goto fail;
    }
    why="Out of memory installing ZSS states";
    staged=calloc(p->state_count,sizeof(*staged));if (staged==NULL) goto fail;
    for (i=0;i<p->state_count;i++) {
        /* Immutable properties stay owned by the program, whose body binding
         * retains it. StateDefs_Free skips these borrowed expressions. */
        staged[i]=p->states[i].properties;
        staged[i].ctrls=KeyedArray_Create(1,sizeof(StateCtrl));
        if (staged[i].ctrls==NULL) goto fail;
        sc.triggers=KeyedArray_Create(1,sizeof(Expr));if (sc.triggers==NULL) goto fail;
        Expr_SetInt(&always,1);
        if (KeyedArray_Add(sc.triggers,&always,0,0)==-1) goto fail;
        sc.type=SCTRL_ZSS_BODY;sc.trigger_groups=1;sc.persistent=1;
        binding=calloc(1,sizeof(*binding));if (binding==NULL) goto fail;
        binding->program=p;binding->state=i;
        {
            const char *trace=getenv("MUGEN_ZSS_TRACE");
            binding->trace_budget=trace!=NULL && !strcmp(trace,"1") ? 4u : 0u;
        }
        p->references++;
        sc.params=binding;
        if (KeyedArray_Add(staged[i].ctrls,&sc,SCTRL_ZSS_BODY,0)==-1) goto fail;
        memset(&sc,0,sizeof(sc));
    }
    merged=KeyedArray_Create(old->count+(int)p->state_count,sizeof(StateDef));
    if (merged==NULL) goto fail;
    for (k=0;k<old->capacity;k++) if (old->slots[k].used &&
        KeyedArray_Add(merged,old->data+old->elem_size*k,old->slots[k].key1,old->slots[k].key2)==-1) goto fail;
    for (i=0;i<p->state_count;i++) if (KeyedArray_Add(merged,&staged[i],p->states[i].number,0)==-1) goto fail;
    /* Only array storage is replaced; existing payload ownership transfers. */
    defs->list=merged;KeyedArray_Free(old);free(staged);return 1;
fail:
    StateCtrl_Free(&sc);
    KeyedArray_Free(merged);
    if (staged!=NULL) {for (i=0;i<p->state_count;i++) free_staged_state(&staged[i]);free(staged);}
    if (error!=NULL) *error=why;
    return 0;
}
