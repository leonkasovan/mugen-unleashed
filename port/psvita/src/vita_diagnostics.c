/* Port addition: R4's native load/presentation checks applied to the real game.
 * The journal is separate from mugen.log so an old generic log cannot hide
 * which executable ran. No display callback calls the engine's text logger.
 * MUGEN_VITA_GL_SELFTEST adds the startup triangle and frame-60 capture. */
#include "mugen.h"
#include "vita_diagnostics.h"
#include "vita_diag_format.h"
#ifndef VITA_DIAG_HOST_TEST
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef MUGEN_VITA_DIAG_BUILD
#define MUGEN_VITA_DIAG_BUILD "mugen-r4-integration-v1"
#endif
static const char *journal_path = "ux0:/data/mugen-vita-gl.log";
static SceUID journal = -1;
static SceDisplayFrameBuf submitted;
static int present_result = -1;
static unsigned int submissions, game_frames;
static int smoke_done, smoke_ok;

void VitaDiag_Log(const char *format, ...)
{
    char text[2048];
    va_list ap;
    int n, offset = 0, written;
    va_start(ap, format);
    n = vsnprintf(text, sizeof(text), format, ap);
    va_end(ap);
    if (journal < 0 || n <= 0) return;
    if ((size_t)n >= sizeof(text)) n = (int)sizeof(text)-1;
    while (offset < n) {
        written = sceIoWrite(journal, text+offset, (unsigned)(n-offset));
        if (written <= 0) break;
        offset += written;
    }
    sceIoSyncByFd(journal, 0);
}

void VitaDiag_Boot(void)
{
    SceKernelModuleInfo info;
    SceUID id;
    char cwd[256];
    int result;
    if (journal >= 0) return;
    journal = sceIoOpen(journal_path, SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC, 0666);
    if (journal < 0) {
        journal_path = "ur0:/data/mugen-vita-gl.log";
        journal = sceIoOpen(journal_path, SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC, 0666);
    }
    VitaDiag_Log("BUILD %s compiled=%s %s unix=%lld log=%s\n",
                 MUGEN_VITA_DIAG_BUILD, __DATE__, __TIME__, (long long)time(NULL), journal_path);
#ifdef MUGEN_VITA_GL_SELFTEST
    VitaDiag_Log("SELFTEST enabled: startup triangle, real shader set, game frame 60\n");
#else
    VitaDiag_Log("SELFTEST disabled: native load/presentation journal only\n");
#endif
    VitaDiag_Log("CWD %s pointer_bits=%u\n", getcwd(cwd,sizeof(cwd)) ? cwd : "UNKNOWN", (unsigned)(8*sizeof(void *)));
    memset(&info,0,sizeof(info)); info.size=sizeof(info);
    id = sceKernelGetModuleIdByAddr((void *)VitaDiag_Boot);
    result = sceKernelGetModuleInfo(id,&info);
    info.module_name[sizeof(info.module_name)-1]=0; info.path[sizeof(info.path)-1]=0;
    VitaDiag_Log("SELF module=0x%08X info=0x%08X name=%s path=%s\n",
                 (unsigned)id,(unsigned)result,info.module_name,info.path);
}

/* Linker wrappers preserve native behavior and exact results, including the
 * compiler load made internally by vitaGL before our public preflight runs. */
int __real_sceKernelLoadStartModule(const char *, SceSize, void *, int, SceKernelLMOption *, int *);
int __wrap_sceKernelLoadStartModule(const char *path, SceSize size, void *args,
                                   int flags, SceKernelLMOption *option, int *status)
{
    int result;
    int compiler = path != NULL && strstr(path,"libshacccg") != NULL;
    if (compiler) VitaDiag_Log("NATIVE LOAD BEGIN %s\n",path);
    result = __real_sceKernelLoadStartModule(path,size,args,flags,option,status);
    if (compiler) VitaDiag_Log("NATIVE LOAD END %s result=0x%08X (%d)\n",path,(unsigned)result,result);
    return result;
}
int __real_sceDisplaySetFrameBuf(const SceDisplayFrameBuf *, SceDisplaySetBufSync);
int __wrap_sceDisplaySetFrameBuf(const SceDisplayFrameBuf *frame, SceDisplaySetBufSync sync)
{
    int old_result = present_result;
    int result = __real_sceDisplaySetFrameBuf(frame,sync);
    present_result = result;
    if (frame != NULL) submitted = *frame;
    else memset(&submitted,0,sizeof(submitted));
    submissions++;
    if (submissions <= 8 || submissions%600 == 0 || old_result != result)
        VitaDiag_Log("PRESENT SET #%u base=%p %ux%u pitch=%u result=0x%08X\n",submissions,
                     submitted.base,(unsigned)submitted.width,(unsigned)submitted.height,
                     (unsigned)submitted.pitch,(unsigned)result);
    return result;
}

int VitaDiag_CheckPresentation(int width, int height, const char *phase)
{
    SceDisplayFrameBuf active;
    int result, accepted;
    /* Called on the game thread after submission, never inside the callback. */
    sceGxmDisplayQueueFinish();
    memset(&active,0,sizeof(active)); active.size=sizeof(active);
    result = sceDisplayGetFrameBuf(&active,SCE_DISPLAY_SETBUF_IMMEDIATE);
    accepted = submitted.pixelformat == SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 &&
        active.pixelformat == SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 &&
        VitaDiag_PresentationAccepted(present_result,result,submitted.base,active.base,
          active.width,active.height,active.pitch,(unsigned)width,(unsigned)height,((unsigned)width+63)&~63u);
    VitaDiag_Log("PRESENT ACTIVE phase=%s get=0x%08X base=%p %ux%u pitch=%u verified=%d\n",
        phase,(unsigned)result,active.base,(unsigned)active.width,(unsigned)active.height,
        (unsigned)active.pitch,accepted);
    if (!accepted) SDL_SetError("Vita display rejected/unbound scanout %ix%i (set 0x%08X, get 0x%08X)",
                                width,height,(unsigned)present_result,(unsigned)result);
    return accepted;
}

#ifdef MUGEN_VITA_GL_SELFTEST
static GLuint VitaDiag_Compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    GLenum error;
    GLsizei length = 0;
    char message[2048];
    if (shader == 0) return 0;
    glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);
    error = glGetError();
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    glGetShaderInfoLog(shader,sizeof(message),&length,message);
    message[sizeof(message)-1]=0;
    VitaDiag_Log("SMOKE COMPILE type=0x%X ok=%d error=0x%X info=%.*s\n",
                 (unsigned)type,(int)ok,(unsigned)error,(int)length,message);
    if (error != GL_NO_ERROR || !ok) { glDeleteShader(shader); return 0; }
    return shader;
}
#endif

int VitaDiag_StartupSmoke(int width, int height)
{
#ifdef MUGEN_VITA_GL_SELFTEST
    static const char *vs = "attribute vec2 aPosition; void main(){gl_Position=vec4(aPosition,0.0,1.0);}";
    static const char *fs = "void main(){gl_FragColor=vec4(0.1,0.9,0.2,1.0);}";
    static const GLfloat vertices[] = {-0.75f,-0.8f,0.75f,-0.8f,0.0f,0.8f};
    GLuint v=0, f=0, p=0;
    GLint linked=0, position=-1;
    GLsizei info_length=0;
    char info[2048]={0};
    GLenum error;
    unsigned char pixel[4]={0};
    int i;
    if (smoke_done) return smoke_ok;
    smoke_done=1;
    VitaDiag_Log("SMOKE BEGIN display=%ix%i\n",width,height);
    if (!VitaGl_InitShaderCompiler()) goto done;
    for (i=0;i<16 && glGetError()!=GL_NO_ERROR;i++) {}
    vglSetSemanticBindingMode(VGL_MODE_SHADER_PAIR);
    v=VitaDiag_Compile(GL_VERTEX_SHADER,vs); if (!v) goto done;
    f=VitaDiag_Compile(GL_FRAGMENT_SHADER,fs); if (!f) goto done;
    p=glCreateProgram(); if (!p) goto done;
    glAttachShader(p,v); glAttachShader(p,f);
    glBindAttribLocation(p,0,"aPosition"); glLinkProgram(p);
    error=glGetError(); glGetProgramiv(p,GL_LINK_STATUS,&linked);
    glGetProgramInfoLog(p,sizeof(info),&info_length,info);
    info[sizeof(info)-1]=0;
    VitaDiag_Log("SMOKE LINK ok=%d error=0x%X info=%.*s\n",(int)linked,(unsigned)error,(int)info_length,info);
    if (!linked || error!=GL_NO_ERROR) goto done;
    position=glGetAttribLocation(p,"aPosition"); if (position<0) goto done;
    glViewport(0,0,width,height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p); glBindBuffer(GL_ARRAY_BUFFER,0);
    glEnableVertexAttribArray((GLuint)position);
    glVertexAttribPointer((GLuint)position,2,GL_FLOAT,GL_FALSE,0,vertices);
    glDrawArrays(GL_TRIANGLES,0,3);
    glDisableVertexAttribArray((GLuint)position);
    glFinish(); glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    error=glGetError();
    smoke_ok=error==GL_NO_ERROR && pixel[0]>=20 && pixel[0]<=32 && pixel[1]>=220 &&
             pixel[1]<=240 && pixel[2]>=44 && pixel[2]<=58 && pixel[3]>=250;
    VitaDiag_Log("SMOKE PIXEL rgba=%u,%u,%u,%u error=0x%X verified=%d\n",
                 pixel[0],pixel[1],pixel[2],pixel[3],(unsigned)error,smoke_ok);
    vglSwapBuffers(GL_FALSE);
    if (!VitaDiag_CheckPresentation(width,height,"smoke")) smoke_ok=0;
done:
    glUseProgram(0);
    if (p) glDeleteProgram(p);
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    /* This runs before Gl_Init sets the real engine state. Leave a clean,
     * black default framebuffer; never overlay the test triangle on gameplay. */
    glViewport(0,0,width,height); glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glFinish();
    vglSwapBuffers(GL_FALSE); sceGxmDisplayQueueFinish();
    VitaDiag_Log("SMOKE RESULT %s\n",smoke_ok ? "PASS" : "FAIL");
    if (!smoke_ok) SDL_SetError("Vita startup smoke test failed; see ux0:/data/mugen-vita-gl.log");
    return smoke_ok;
#else
    (void)width; (void)height; (void)smoke_done; (void)smoke_ok;
    return 1;
#endif
}

#ifdef MUGEN_VITA_GL_SELFTEST
static int VitaDiag_Capture(void)
{
    SceDisplayFrameBuf active;
    unsigned char header[54], row[960*4];
    SceUID file;
    int result, y, ok=1;
    memset(&active,0,sizeof(active)); active.size=sizeof(active);
    result=sceDisplayGetFrameBuf(&active,SCE_DISPLAY_SETBUF_IMMEDIATE);
    if (result<0 || active.pixelformat!=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 ||
        !VitaDiag_ValidCapture(active.base,active.width,active.height,active.pitch) ||
        !VitaDiag_BmpHeader(header,active.width,active.height)) {
        VitaDiag_Log("GAME CAPTURE REJECTED: invalid active framebuffer\n"); return 0;
    }
    file=sceIoOpen("ux0:/data/mugen-vita-frame60.bmp",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    if (file<0) { VitaDiag_Log("GAME CAPTURE OPEN FAIL 0x%08X\n",(unsigned)file); return 0; }
    if (sceIoWrite(file,header,sizeof(header))!=(int)sizeof(header)) ok=0;
    for (y=(int)active.height-1;ok && y>=0;y--) {
        const unsigned char *src=(const unsigned char *)active.base+(size_t)y*active.pitch*4;
        VitaDiag_BmpRow(row,src,active.width);
        if (sceIoWrite(file,row,active.width*4)!=(int)(active.width*4)) ok=0;
    }
    sceIoClose(file);
    VitaDiag_Log("GAME CAPTURE %s frame=%u %ux%u path=ux0:/data/mugen-vita-frame60.bmp\n",
                 ok ? "SAVED" : "FAILED",game_frames,(unsigned)active.width,(unsigned)active.height);
    return ok;
}
#endif

void VitaDiag_GamePresent(int width, int height)
{
    game_frames++;
    if (game_frames==1 || game_frames==60 || game_frames%600==0) {
        int accepted=VitaDiag_CheckPresentation(width,height,"game");
        VitaDiag_Log("GAME FRAME %u presentation=%s\n",game_frames,accepted ? "PASS" : "FAIL");
#ifdef MUGEN_VITA_GL_SELFTEST
        if (game_frames==60 && accepted) VitaDiag_Capture();
#endif
    }
}
