/* Exercise the production lexer without an engine, window or Go runtime. */
#include "zss_lex.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void bad(const char *s, size_t n, size_t limit)
{
    ZssLexer l;
    assert(ZssLex_Init(&l,s,n,limit));
    while (ZssLex_Next(&l)>0) {}
    assert(l.token.kind==ZSS_ERROR && l.error);
    assert(ZssLex_Next(&l)==ZSS_ERROR);
}
int main(void)
{
    ZssLexer l;
    const char s[]="\xef\xbb\xbf# hi\r\n[StateDef 200; type: S;]\nlet x = .5e-2;\n$x := 1; # tail";
    int expected[]={'[',ZSS_IDENT,ZSS_INT,';',ZSS_IDENT,':',ZSS_IDENT,';',']',ZSS_IDENT,ZSS_IDENT,'=',ZSS_FLOAT,';',ZSS_LOCAL,ZSS_ASSIGN,ZSS_INT,';',ZSS_EOF};
    size_t i;
    assert(ZssLex_Init(&l,s,sizeof(s)-1,128));
    for(i=0;i<sizeof(expected)/sizeof(*expected);i++) {
        assert(ZssLex_Next(&l)==expected[i]);
        if(i==0) assert(l.token.line==2 && l.token.column==1 && l.token.offset==9);
        if(i==1) assert(ZssLex_Is(&l,"STATEDEF") && !ZssLex_Is(&l,"state"));
        if(i==12) assert(l.token.length==5);
    }
    assert(ZssLex_Next(&l)==ZSS_EOF);
    assert(ZssLex_Init(&l,"\"MiX;#{}\"",9,32));
    assert(ZssLex_Next(&l)==ZSS_STRING && l.token.length==9);
    assert(!ZssLex_Is(&l,"mix"));
    { const char ops[]="!= >= <= && || ^^ ** : ="; int kinds[]={ZSS_NE,ZSS_GE,ZSS_LE,ZSS_AND,ZSS_OR,ZSS_XOR,ZSS_POWER,':','='};
      assert(ZssLex_Init(&l,ops,strlen(ops),4));
      for(i=0;i<sizeof(kinds)/sizeof(*kinds);i++) assert(ZssLex_Next(&l)==kinds[i]); }
    bad("\"oops",5,32);bad("\"a\nb\"",5,32);bad("$",1,32);bad("1e+",3,32);
    bad("abcd",4,3);bad("1234",4,3);bad("@",1,32);bad("a\0b",3,32);bad("#a\0",3,32);
    assert(!ZssLex_Init(&l,NULL,1,32));assert(!ZssLex_Init(&l,"",0,0));
    assert(ZssLex_Init(&l,NULL,0,1));assert(ZssLex_Next(&l)==ZSS_EOF);
    /* Buffers are length-delimited, not required to have a trailing NUL. */
    {char one='x';assert(ZssLex_Init(&l,&one,1,1));assert(ZssLex_Next(&l)==ZSS_IDENT);assert(ZssLex_Next(&l)==ZSS_EOF);}
    puts("ok: ZSS tokens, spans, comments, strings, bounds and terminal errors");
    return 0;
}
