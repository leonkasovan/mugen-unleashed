/* Port addition: explicit st/stN/stcommon=.zss loading through the Stream API.
 * Constants, commands and ordinary CNS files retain their original loaders. */
#include "mugen.h"
#include "zss_params.h"
#include "zss_binding.h"
StateDefs *Zss_LoadStateStreamNew(Stream *stream, CharData *owner, ZssDiagnostic *d)
{
    StateDefs *defs=calloc(1,sizeof(*defs));
    if (defs!=NULL) {
        defs->list=KeyedArray_Create(1,sizeof(StateDef));
        if (defs->list!=NULL) {
            if (Zss_LoadStateStream(stream,defs,owner,d)) return defs;
            StateDefs_Free(defs);return NULL;
        }
        free(defs);
    }
    if (d!=NULL) {
        memset(d,0,sizeof(*d));d->line=d->column=1;
        d->message="Out of memory creating ZSS state collection";
    }
    return NULL;
}
int Zss_LoadStateStream(Stream *stream, StateDefs *defs, CharData *owner, ZssDiagnostic *d)
{
    char *source=NULL, *grown, extra;
    size_t size=0, capacity=4096, n;
    ZssProgram *program=NULL;
    const char *message="Cannot read ZSS state source";
    int ok=0;
    if (d!=NULL) {memset(d,0,sizeof(*d));d->line=d->column=1;}
    if (stream==NULL || defs==NULL || owner==NULL) goto done;
    source=calloc(1,capacity+1);
    if (source==NULL) {message="Out of memory reading ZSS source";goto done;}
    for (;;) {
        if (size==capacity) {
            if (capacity==ZSS_MAX_SOURCE) {
                g_streamError=0;n=Stream_Read(&extra,1,1,stream);
                if (n) {message="ZSS source exceeds prototype size limit";goto done;}
                if (g_streamError && g_streamError!=STREAM_ERR_EOF) goto done;
                break;
            }
            capacity*=2;
            grown=calloc(1,capacity+1);
            if (grown==NULL) {message="Out of memory reading ZSS source";goto done;}
            memcpy(grown,source,size);free(source);source=grown;
        }
        g_streamError=0;n=Stream_Read(source+size,1,capacity-size,stream);
        if (g_streamError && g_streamError!=STREAM_ERR_EOF) goto done;
        size+=n;
        if (size<capacity) break;
    }
    if (!Zss_CompileProgram(source,size,owner,&program,d)) goto cleanup;
    ok=Zss_InstallProgram(program,defs,&message);
    if (ok) goto cleanup;
done:
    if (d!=NULL) d->message=message;
cleanup:
    if (!ok && d!=NULL) {
        /* Early compiler OOM and post-compile installation errors can lack a
         * token location. Report file start rather than an invalid line zero. */
        if (!d->line) d->line=1;
        if (!d->column) d->column=1;
    }
    Zss_FreeProgram(program);free(source);return ok;
}
