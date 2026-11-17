/* Linux native-test companion: deliver a real SDL key event after the existing
 * bounded ZSS trace proves the synthetic state's first update has completed.
 * No engine symbol, private memory or production test hook is accessed. */
#include "mugen.h"
#include <dlfcn.h>

static int state_ready(void)
{
    char tail[8192];
    FILE *file=fopen("mugen.log","rb");
    long size;
    size_t bytes;
    if (file==NULL) return 0;
    if (fseek(file,0,SEEK_END)!=0 || (size=ftell(file))<0 ||
        fseek(file,size>(long)sizeof(tail)-1 ? size-(long)sizeof(tail)+1 : 0,SEEK_SET)!=0) {
        fclose(file);return 0;
    }
    bytes=fread(tail,1,sizeof(tail)-1,file);tail[bytes]=0;fclose(file);
    return strstr(tail,"ZSS state 0 body result 0\n")!=NULL;
}
int SDL_PollEvent(SDL_Event *event)
{
    static int (*poll_event)(SDL_Event *);
    static int sent;
    int result;
    if (poll_event==NULL) {
        void *symbol=dlsym(RTLD_NEXT,"SDL_PollEvent");
        _Static_assert(sizeof(poll_event)==sizeof(symbol),"POSIX function pointer size");
        if (symbol==NULL) {fputs("zss-keyinject: missing SDL_PollEvent\n",stderr);exit(2);}
        memcpy(&poll_event,&symbol,sizeof(poll_event));
    }
    result=poll_event(event);
    if (result==0 && event!=NULL && !sent && state_ready()) {
        memset(event,0,sizeof(*event));event->type=SDL_KEYDOWN;
        event->key.state=SDL_PRESSED;event->key.timestamp=SDL_GetTicks();
        event->key.keysym.sym=SDLK_DOWN;event->key.keysym.scancode=SDL_SCANCODE_DOWN;
        sent=1;fputs("zss-keyinject: down\n",stderr);return 1;
    }
    return result;
}
