/* Port addition: owned ZSS state programs and structured body execution.
 * Kept separate from original StateDefs/Player layouts. This prototype rejects
 * features that need the pending expression-context and runtime integrations. */
#include "mugen.h"
#include "zss_params.h"
#include "zss_statedef.h"
#include "zss_program.h"
#define NO_NODE UINT32_MAX
#define MAX_NODES 1024u
#define MAX_STATES 64u
#define MAX_DEPTH 32u

typedef struct ZssNode {
    int branch;
    uint32_t next, child, otherwise;
    Expr condition;
    StateCtrl controller;
} ZssNode;
typedef struct ZssState {
    int number;
    uint32_t first;
    StateDef properties;
} ZssState;
struct ZssProgram {
    ZssNode *nodes;
    ZssState *states;
    uint32_t node_count, node_capacity, state_count, state_capacity;
};
typedef struct ZssParser {
    ZssLexer lex;
    ZssProgram *program;
    CharData *owner;
    ZssDiagnostic *diagnostic;
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
static int supported_token(ZssParser *p)
{
    if (p->lex.token.kind==ZSS_LOCAL || p->lex.token.kind==ZSS_AND || p->lex.token.kind==ZSS_OR)
        return error(p,"Locals and lazy logical operators require pending ZSS expression contexts");
    return 1;
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
        if (!supported_token(p)) return 0;
        if ((header && (kind=='[' || kind=='(')) || (!header && kind=='{')) depth++;
        if ((header && (kind==']' || kind==')')) || (!header && kind=='}')) {
            if (--depth==0) return kind==(header?']':'}') || error(p,"Wrong closing delimiter");
        }
        if (depth>MAX_DEPTH) return error(p,"ZSS delimiter nesting limit");
    }
    return error(p,"Unclosed ZSS header or controller");
}
static int body(ZssParser *p, unsigned depth, uint32_t *first);
static int branch(ZssParser *p, unsigned depth, uint32_t *index, uint32_t *end_line)
{
    uint32_t id=node(p), child=NO_NODE, other=NO_NODE;
    char *expression, *write;
    const char *end;
    int tokens=0, kind;
    if (id==NO_NODE) return 0;
    if (depth>=MAX_DEPTH) return error(p,"ZSS block nesting limit");
    p->program->nodes[id].branch=1;
    expression=calloc(1,p->lex.size*2+1);
    if (expression==NULL) return error(p,"Out of memory compiling condition");
    write=expression;
    while ((kind=next(p))!='{') {
        if (kind<=0 || kind=='}' || !supported_token(p)) {
            free(expression);return error(p,"Invalid or unsupported if condition");
        }
        if (++tokens>=EXPR_OP_STACK_SIZE) {free(expression);return error(p,"Condition token limit");}
        if (tokens>1) *write++=' ';
        memcpy(write,p->lex.source+p->lex.token.offset,p->lex.token.length);write+=p->lex.token.length;
    }
    end=expression;
    if (!tokens || !Expr_Compile(expression,&p->program->nodes[id].condition,p->owner,&end,EXPR_WANT_ANY) || *end) {
        free(expression);return error(p,"Condition rejected by MUGEN expression compiler");
    }
    free(expression);
    next(p);
    if (!body(p,depth+1,&child)) return 0;
    if (p->lex.token.kind!='}') return error(p,"Expected '}' after if body");
    *end_line=p->lex.token.line;next(p);
    if (ZssLex_Is(&p->lex,"else")) {
        next(p);
        if (ZssLex_Is(&p->lex,"if")) {
            if (!branch(p,depth+1,&other,end_line)) return 0;
        } else {
            if (p->lex.token.kind!='{') return error(p,"Expected if or '{' after else");
            next(p);
            if (!body(p,depth+1,&other)) return 0;
            if (p->lex.token.kind!='}') return error(p,"Expected '}' after else body");
            *end_line=p->lex.token.line;next(p);
        }
    }
    /* Recursion may grow the node arena; retain indices rather than pointers. */
    p->program->nodes[id].child=child;p->program->nodes[id].otherwise=other;
    *index=id;return 1;
}
static int body(ZssParser *p, unsigned depth, uint32_t *first)
{
    uint32_t last=NO_NODE, id, end_line;
    ZssToken start;
    int kind;
    *first=NO_NODE;
    for (;;) {
        kind=p->lex.token.kind;
        if (kind==ZSS_EOF || kind=='}' || kind=='[') return 1;
        if (kind!=ZSS_IDENT) return error(p,"Expected a controller or if statement");
        if (ZssLex_Is(&p->lex,"if")) {
            if (!branch(p,depth,&id,&end_line)) return 0;
        } else {
            start=p->lex.token;
            if (next(p)!='{') return error(p,"Unsupported ZSS statement or missing '{'");
            if (!enclosed(p,0)) return 0;
            id=node(p);if (id==NO_NODE) return 0;
            if (!Zss_CompileController(p->lex.source+start.offset,p->lex.pos-start.offset,
                                      p->owner,&p->program->nodes[id].controller,p->diagnostic)) {
                relocate(p->diagnostic,&start);return 0;
            }
            end_line=p->lex.token.line;next(p);
        }
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
    if (diagnostic!=NULL) memset(diagnostic,0,sizeof(*diagnostic));
    p.lex.token.line=p.lex.token.column=1;
    if (source==NULL || owner==NULL || out==NULL || size>ZSS_MAX_SOURCE)
        return error(&p,"Invalid ZSS program source");
    if (!ZssLex_Init(&p.lex,source,size,65536)) return error(&p,p.lex.error);
    p.program=calloc(1,sizeof(*p.program));
    if (p.program==NULL) return error(&p,"Out of memory creating ZSS program");
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
        if (!body(&p,0,&first)) goto fail;
        p.program->states[i].first=first;
    }
    if (!p.program->state_count) {error(&p,"Program needs a StateDef");goto fail;}
    *out=p.program;return 1;
fail:
    StateDef_FreeProperties(&state.properties);Zss_FreeProgram(p.program);return 0;
}
void Zss_FreeProgram(ZssProgram *p)
{
    uint32_t i;
    if (p==NULL) return;
    for (i=0;i<p->node_count;i++) {Expr_Free(&p->nodes[i].condition);StateCtrl_Free(&p->nodes[i].controller);}
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
int Zss_RunBody(ZssProgram *p, size_t state, Player *player,
                ZssControllerRun run, void *context, int *result)
{
    uint32_t pending[MAX_DEPTH+1], top=0, index;
    int bits=0, i=0, type, selected;
    float f=0;
    ZssNode *n;
    if (p==NULL || state>=p->state_count || player==NULL || run==NULL || result==NULL) return 0;
    index=p->states[state].first;
    while (index!=NO_NODE) {
        n=&p->nodes[index];
        if (n->branch) {
            type=Expr_Eval(player,&n->condition,&i,&f);
            if (type==0) return 0;
            selected=type==1 ? i!=0 : type==2 ? f!=0 : 0;
            if (n->next!=NO_NODE) {
                if (top>=MAX_DEPTH+1) return 0;
                pending[top++]=n->next;
            }
            index=selected?n->child:n->otherwise;
        } else {
            bits|=run(player,&n->controller,context);
            if (bits&STATECTRL_STOP) {*result=bits;return 1;}
            index=n->next;
        }
        while (index==NO_NODE && top) index=pending[--top];
    }
    *result=bits;return 1;
}
