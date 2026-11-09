/* Executes the production diagnostics module with mocked hardware.
   Checks failure guards and capture timing, not native graphics correctness. */
#define MUGEN_H
#define VITA_DIAG_HOST_TEST 1
#define MUGEN_VITA_GL_SELFTEST 1
#include "test_vita_diagnostics_api.h"
#include "../port/psvita/src/vita_diagnostics.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *files[8];static uint32_t *buffer;static SceDisplayFrameBuf active;
static int draws,swaps,compiles,captures,drew;
static GLuint program,shaders,programs;static GLint view[4];
static int native_fail, shader_fail, pixel_fail, links;
static char last_error[256];
static void SDL_SetError(const char *format, ...) { (void)format;strcpy(last_error,"set"); }
static int VitaGl_InitShaderCompiler(void) { return 1; }
int __wrap_sceKernelLoadStartModule(const char *,SceSize,void *,int,SceKernelLMOption *,int *);extern int __wrap_sceDisplaySetFrameBuf(const SceDisplayFrameBuf *,SceDisplaySetBufSync);

SceUID sceIoOpen(const char *p,int f,int mode) {
 if (f&SCE_O_WRONLY) {int fd=strstr(p,".bmp") ? 4 : 3;assert(strstr(p,"mugen-vita-"));files[fd]=fopen(fd==4 ? "host-capture.bmp" : "host.log","wb");assert(files[fd]);if(fd==4)captures++;return fd;}
 return strstr(p,"external/") ? -2 : 6;
}
int sceIoClose(SceUID fd){if(fd<6 && files[fd]){fclose(files[fd]);files[fd]=NULL;}return 0;}
int sceIoWrite(SceUID fd,const void *p,unsigned int n){assert(files[fd]);return (int)fwrite(p,1,n,files[fd]);}
int sceIoSyncByFd(SceUID fd,int f){return fflush(files[fd]);}
SceUID sceKernelGetModuleIdByAddr(void *p){return 1;}
int sceKernelGetModuleInfo(SceUID id,SceKernelModuleInfo *p){strcpy(p->module_name,id==2 ? "SceShaccCg" : "host-diag");strcpy(p->path,id==2 ? "ur0:/data/libshacccg.suprx" : "ux0:/app/MSDK00001/eboot.bin");return 0;}
int sceDisplayGetFrameBuf(SceDisplayFrameBuf *p,SceDisplaySetBufSync s){*p=active;return 0;}
void sceGxmDisplayQueueFinish(void){}
int __real_sceDisplaySetFrameBuf(const SceDisplayFrameBuf *p,SceDisplaySetBufSync s){assert(p && p->width==960 && p->height==544);if(native_fail)return -7;active=*p;return 0;}
int __real_sceKernelLoadStartModule(const char *p,SceSize n,void *a,int f,SceKernelLMOption *o,int *s){return 2;}
GLboolean vglInitWithCustomSizes(int pool,int w,int h,int ram,int cdram,int phy,int dialog,int msaa){assert(w==960 && h==544);buffer=calloc((size_t)w*h,4);assert(buffer);view[2]=w;view[3]=h;__wrap_sceKernelLoadStartModule("ur0:/data/libshacccg.suprx",0,NULL,0,NULL,NULL);return GL_FALSE;}
void vglWaitVblankStart(GLboolean b){}void vglSetSemanticBindingMode(GLenum m){}
void vglSwapBuffers(GLboolean b){SceDisplayFrameBuf fb={sizeof(fb),buffer,960,0,960,544};swaps++;__wrap_sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);}
GLenum glGetError(void){return GL_NO_ERROR;}GLuint glCreateShader(GLenum t){return ++shaders;}void glDeleteShader(GLuint s){}
void glShaderSource(GLuint s,GLsizei n,const GLchar *const *p,const GLint *len){}
void glCompileShader(GLuint s){compiles++;}
void glGetShaderiv(GLuint s,GLenum p,GLint *v){*v=shader_fail ? GL_FALSE : GL_TRUE;}void glGetShaderInfoLog(GLuint s,GLsizei m,GLsizei *n,GLchar *p){*n=0;p[0]=0;}
GLuint glCreateProgram(void){return ++programs;}void glDeleteProgram(GLuint p){}void glAttachShader(GLuint p,GLuint s){}void glBindAttribLocation(GLuint p,GLuint n,const GLchar *s){}void glLinkProgram(GLuint p){assert(!shader_fail);links++;}
void glGetProgramiv(GLuint p,GLenum n,GLint *v){*v=GL_TRUE;}void glGetProgramInfoLog(GLuint p,GLsizei m,GLsizei *n,GLchar *s){*n=0;s[0]=0;}
GLint glGetUniformLocation(GLuint p,const GLchar *s){return 73;}GLint glGetAttribLocation(GLuint p,const GLchar *s){return 0;}
void glBindBuffer(GLenum t,GLuint p){assert(t==GL_ARRAY_BUFFER && p==0);}
void glUseProgram(GLuint p){program=p;}void glDisable(GLenum e){}void glClearColor(GLfloat a,GLfloat b,GLfloat c,GLfloat d){}
void glClear(GLenum e){drew=0;}void glViewport(GLint x,GLint y,GLsizei w,GLsizei h){view[0]=x;view[1]=y;view[2]=w;view[3]=h;}
void glEnableVertexAttribArray(GLuint p){}void glDisableVertexAttribArray(GLuint p){}void glVertexAttribPointer(GLuint p,GLint n,GLenum t,GLboolean b,GLsizei s,const void *v){}
void glDrawArrays(GLenum m,GLint first,GLsizei count){assert(program==1 && count==3);draws++;drew=1;}
void glReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum f,GLenum t,void *p){unsigned char rgba[]={25,229,51,255};assert(drew && x==480 && y==272 && w==1 && h==1);if(pixel_fail)rgba[0]=0;memcpy(p,rgba,4);}
void glFinish(void){}void glGetIntegerv(GLenum p,GLint *v){memcpy(v,view,sizeof(view));}const GLubyte *glGetString(GLenum p){return (const GLubyte *)"HOST MOCK";}
#include "../port/psvita/src/vita_diagnostics.c"
int main(int argc,char **argv)
{
    unsigned char header[54],pixel[4];
    FILE *f;
    int ok;
    assert(argc==2);
    native_fail=!strcmp(argv[1],"native-fail");
    shader_fail=!strcmp(argv[1],"shader-fail");
    pixel_fail=!strcmp(argv[1],"pixel-fail");
    VitaDiag_Boot();
    vglInitWithCustomSizes(0,960,544,0,0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);vglSwapBuffers(GL_FALSE);
    ok=VitaDiag_CheckPresentation(960,544,"create");
    assert(ok==!native_fail);
    ok=VitaDiag_StartupSmoke(960,544);
    assert(ok==!(native_fail || shader_fail || pixel_fail));
    if (shader_fail) assert(!links && !draws && last_error[0]);
    if (pixel_fail || native_fail) assert(last_error[0]);
    assert(captures==0); /* Startup swaps never count as game frames. */
    for (int frame=1;frame<=60;frame++) {
        for (unsigned int i=0;i<960*544;i++)buffer[i]=0xff332211u;
        vglSwapBuffers(GL_FALSE);VitaDiag_GamePresent(960,544);
        assert(captures==(frame==60 && !native_fail));
    }
    if (!native_fail) {
        f=fopen("host-capture.bmp","rb");assert(f);
        assert(fread(header,1,54,f)==54 && fread(pixel,1,4,f)==4);
        assert(header[0]=='B' && header[1]=='M');
        assert(pixel[0]==0x33 && pixel[1]==0x22 && pixel[2]==0x11);
        fseek(f,0,SEEK_END);assert(ftell(f)==2089014);fclose(f);
    }
    sceIoClose(journal);free(buffer);
    puts("ok: real Vita diagnostic module, native load/present, smoke failure guards and real-buffer frame-60 capture");
    return 0;
}
