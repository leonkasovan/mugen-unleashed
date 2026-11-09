/* Host-only API declarations. Tests control flow, not a Vita GPU. */
#ifndef MUGEN_VITA_DIAG_TEST_API_H
#define MUGEN_VITA_DIAG_TEST_API_H
#include <stddef.h>
#include <stdint.h>
typedef unsigned int GLenum,GLuint;typedef int GLint,GLsizei;typedef unsigned char GLboolean,GLubyte;typedef float GLfloat;typedef char GLchar;
typedef int SceUID,SceDisplaySetBufSync;typedef void SceKernelLMOption;typedef unsigned int SceSize;
typedef struct {unsigned int size;void *base;unsigned int pitch,pixelformat,width,height;} SceDisplayFrameBuf;
typedef struct {unsigned int size;char module_name[28];char path[256];} SceKernelModuleInfo;
enum {GL_FALSE=0,GL_TRUE=1,GL_NO_ERROR=0,GL_VERTEX_SHADER=2,GL_FRAGMENT_SHADER,GL_COMPILE_STATUS,GL_LINK_STATUS,GL_SCISSOR_TEST,GL_DEPTH_TEST,GL_BLEND,GL_COLOR_BUFFER_BIT,GL_FLOAT,GL_TRIANGLES,GL_RGBA,GL_UNSIGNED_BYTE,GL_VIEWPORT,GL_VERSION,GL_VENDOR,GL_RENDERER,GL_ARRAY_BUFFER,GL_DEPTH_BUFFER_BIT};
enum {SCE_O_RDONLY=1,SCE_O_WRONLY=2,SCE_O_CREAT=4,SCE_O_TRUNC=8,SCE_CTRL_MODE_ANALOG=0,SCE_CTRL_START=1,SCE_CTRL_CROSS=2,SCE_CTRL_TRIANGLE=4,SCE_GXM_MULTISAMPLE_NONE=0,SCE_DISPLAY_SETBUF_IMMEDIATE=0,SCE_DISPLAY_SETBUF_NEXTFRAME=1,SCE_DISPLAY_PIXELFORMAT_A8B8G8R8=0,VGL_MODE_SHADER_PAIR=0,EGL_WIDTH=1,EGL_HEIGHT=2,EGL_TRUE=1};
SceUID sceIoOpen(const char *,int,int);int sceIoClose(SceUID);int sceIoWrite(SceUID,const void *,unsigned int);int sceIoSyncByFd(SceUID,int);
SceUID sceKernelGetModuleIdByAddr(void *);int sceKernelGetModuleInfo(SceUID,SceKernelModuleInfo *);
int sceDisplayGetFrameBuf(SceDisplayFrameBuf *,SceDisplaySetBufSync);void sceGxmDisplayQueueFinish(void);
GLboolean vglInitWithCustomSizes(int,int,int,int,int,int,int,int);void vglWaitVblankStart(GLboolean);void vglSetSemanticBindingMode(GLenum);void vglSwapBuffers(GLboolean);
GLenum glGetError(void);GLuint glCreateShader(GLenum);void glDeleteShader(GLuint);void glShaderSource(GLuint,GLsizei,const GLchar *const *,const GLint *);void glCompileShader(GLuint);void glGetShaderiv(GLuint,GLenum,GLint *);void glGetShaderInfoLog(GLuint,GLsizei,GLsizei *,GLchar *);
GLuint glCreateProgram(void);void glDeleteProgram(GLuint);void glAttachShader(GLuint,GLuint);void glBindAttribLocation(GLuint,GLuint,const GLchar *);void glLinkProgram(GLuint);void glGetProgramiv(GLuint,GLenum,GLint *);void glGetProgramInfoLog(GLuint,GLsizei,GLsizei *,GLchar *);GLint glGetUniformLocation(GLuint,const GLchar *);GLint glGetAttribLocation(GLuint,const GLchar *);
void glBindBuffer(GLenum,GLuint);void glUseProgram(GLuint);void glDisable(GLenum);void glClearColor(GLfloat,GLfloat,GLfloat,GLfloat);void glClear(GLenum);void glViewport(GLint,GLint,GLsizei,GLsizei);void glEnableVertexAttribArray(GLuint);void glDisableVertexAttribArray(GLuint);void glVertexAttribPointer(GLuint,GLint,GLenum,GLboolean,GLsizei,const void *);void glDrawArrays(GLenum,GLint,GLsizei);void glReadPixels(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *);void glFinish(void);void glGetIntegerv(GLenum,GLint *);const GLubyte *glGetString(GLenum);
#endif
