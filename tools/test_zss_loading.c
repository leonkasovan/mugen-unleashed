/* Test the real character loader's ZSS resource helper. Stream and compiler
 * calls are explicit boundaries here; test_cns_abi exercises the real compiler.
 * In particular, an archive belongs to Char_Load, including on failed loads. */
#include "mugen.h"
#include <assert.h>
#include "../src/char.c"

static Stream archive, local, fallback;
static StateDefs collection;
static int local_ok, zip_ok, find_ok, fallback_ok, compile_ok;
static int closes, unloads, opens, searches, creates, appends, errors;

void Path_Join(const char *name, const char *dir, char *path)
{
    assert(!strcmp(dir,"chars/test"));
    snprintf(path,512,"%s/%s",dir,name);
}
Stream *Stream_OpenFile(const char *path, const char *mode)
{
    assert(!strcmp(mode,"rt"));opens++;
    if (!strcmp(path,"data/common.cns.zss")) return fallback_ok ? &fallback : NULL;
    assert(!strcmp(path,"chars/test/states.ZsS"));
    return local_ok ? &local : NULL;
}
int File_Find(const char *name, char *out, char *dir, const char *base)
{
    assert(!strcmp(name,"states.ZsS") && !dir && !base);searches++;
    strcpy(out,"data/common.cns.zss");return find_ok;
}
int Stream_ZipUnload(Stream *stream)
{
    assert(stream==&archive);unloads++;return 1;
}
int Stream_ZipLoad(Stream *stream, const char *name)
{
    assert(stream==&archive && !strcmp(name,"states.ZsS"));return zip_ok;
}
int Stream_Close(Stream *stream)
{
    assert(stream==&local || stream==&fallback);closes++;return 0;
}
void Error_Printf(const char *fmt, ...) { (void)fmt;errors++; }
static int compile(Stream *stream, CharData *owner, ZssDiagnostic *d)
{
    assert(stream==&local || stream==&fallback || stream==&archive);
    assert(owner);memset(d,0,sizeof(*d));
    d->line=d->column=1;d->message="injected compiler failure";
    return compile_ok;
}
StateDefs *Zss_LoadStateStreamNew(Stream *stream, CharData *owner, ZssDiagnostic *d)
{
    creates++;return compile(stream,owner,d) ? &collection : NULL;
}
int Zss_LoadStateStream(Stream *stream, StateDefs *defs, CharData *owner, ZssDiagnostic *d)
{
    assert(defs==&collection);appends++;return compile(stream,owner,d);
}
static void reset(CharData *owner)
{
    memset(owner,0,sizeof(*owner));local_ok=zip_ok=fallback_ok=compile_ok=1;find_ok=0;
    closes=unloads=opens=searches=creates=appends=errors=0;
}
int main(void)
{
    CharData owner;
    int zip, common, fail;
    assert(Char_IsZssSource("states.ZsS") && Char_IsZssSource("common.cns.zss"));
    assert(!Char_IsZssSource("") && !Char_IsZssSource("zss"));
    assert(!Char_IsZssSource("foo.zss.cns") && !Char_IsZssSource("foo.cns"));
    for(zip=0;zip<=1;zip++) for(common=0;common<=1;common++) for(fail=0;fail<=1;fail++) {
        reset(&owner);compile_ok=!fail;
        assert(Char_LoadZssStates(zip,&archive,"chars/test","states.ZsS",common,&owner)==!fail);
        assert(creates==1 && !appends && errors==fail);
        assert(closes==!zip && unloads==zip && opens==!zip && !searches);
        assert(owner.states==(fail ? NULL : &collection));
        reset(&owner);owner.states=&collection;compile_ok=!fail;
        assert(Char_LoadZssStates(zip,&archive,"chars/test","states.ZsS",common,&owner)==!fail);
        assert(!creates && appends==1 && errors==fail && closes==!zip);
        assert(owner.states==&collection);
    }
    for(zip=0;zip<=1;zip++) for(fail=0;fail<=1;fail++) {
        reset(&owner);local_ok=zip_ok=0;find_ok=1;compile_ok=!fail;
        assert(Char_LoadZssStates(zip,&archive,"chars/test","states.ZsS",1,&owner)==!fail);
        assert(searches==1 && closes==1 && opens==2-zip && errors==fail);
        /* Missing primary/additional sources never use common-state fallback. */
        reset(&owner);local_ok=zip_ok=0;find_ok=1;
        assert(!Char_LoadZssStates(zip,&archive,"chars/test","states.ZsS",0,&owner));
        assert(!searches && !creates && !appends && !closes && errors==1);
        reset(&owner);local_ok=zip_ok=0;
        assert(!Char_LoadZssStates(zip,&archive,"chars/test","states.ZsS",1,&owner));
        assert(searches==1 && !creates && !closes && errors==1);
        reset(&owner);local_ok=zip_ok=fallback_ok=0;find_ok=1;
        assert(!Char_LoadZssStates(zip,&archive,"chars/test","states.ZsS",1,&owner));
        assert(searches==1 && opens==2-zip && !creates && !closes && errors==1);
    }
    puts("ok: ZSS suffix dispatch, character/common lookup and borrowed archive ownership");
    return 0;
}
