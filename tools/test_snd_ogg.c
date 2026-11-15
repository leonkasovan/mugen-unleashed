#include "mugen.h"
#include <SDL_mixer.h>
#include <assert.h>
#include <stdarg.h>
#include <libresample.h>
/* Decode actual converted bank entries with real SDL2_mixer; no game window. */
int g_outOfMemory,g_streamError,g_sndResampleMethod=SND_RESAMPLE_LIBRESAMPLE;
AudioFormat g_sndFormat={44100,AUDIO_FMT_S16,2};
Resampler *g_sndResampler;
const ResamplerVtbl g_resamplerVtbl={Resampler_Open,Resampler_Create,Resampler_Close,Resampler_InputNeeded,Resampler_State,Resampler_Write,Resampler_Read,Resampler_InFormat,Resampler_OutFormat,Resampler_Reset,Resampler_InBlock,Resampler_OutBlock};
void MatchLog_Printf(int level,const char*fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
void Log_Printf(const char*fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
void *Mem_Calloc(size_t n,size_t sz){return calloc(n,sz);}
static unsigned int u32(FILE*f){unsigned char b[4];assert(fread(b,1,4,f)==4);return b[0]|((unsigned)b[1]<<8)|((unsigned)b[2]<<16)|((unsigned)b[3]<<24);}
int main(int argc,char**argv){
 if (argc != 2) { fprintf(stderr,"usage: %s mixed-bank.snd\n",argv[0]); return 2; }
 int quality=0;unsigned count,offset,ogg=0,wav=0;size_t pcm=0;
 SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 assert(SDL_Init(SDL_INIT_AUDIO)==0);assert(Mix_OpenAudio(44100,AUDIO_S16SYS,2,1024)==0);
 resample_init(1,0);g_sndResampler=Resampler_Create(&g_sndFormat,1024,&quality);assert(g_sndResampler);
 FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,16,SEEK_SET);count=u32(f);offset=u32(f);
 for(unsigned i=0;i<count;i++){
  fseek(f,offset,SEEK_SET);unsigned next=u32(f),len=u32(f);u32(f);u32(f);
  unsigned char *data=malloc(len);assert(data&&fread(data,1,len,f)==len);
  Stream s={.type=STREAM_MEMORY,.begin=data,.pos=data,.end=data+len};int status=0;
  Mix_Chunk*c=Snd_DecodeWav(&s,&status);assert(c&&status==0&&Stream_Tell(&s)==0);
  if(!memcmp(data,"OggS",4)){assert(c->allocated==0);ogg++;}else wav++;
  pcm+=c->alen;void*owned=c->allocated?NULL:c->abuf;Mix_FreeChunk(c);free(owned);free(data);offset=next;
 }
 fclose(f);
 unsigned char bad[]="OggSbad";Stream s={.type=STREAM_MEMORY,.begin=bad,.pos=bad,.end=bad+sizeof(bad)};int status=0;
 assert(Snd_DecodeWav(&s,&status)==NULL&&status!=0&&s.pos==s.begin);
 free(g_sndResampler->quality);free(g_sndResampler);resample_deinit();Mix_CloseAudio();Mix_Quit();SDL_Quit();
 printf("PASS: %u Ogg + %u WAV, %zu PCM bytes; invalid Ogg rejected\n",ogg,wav,pcm);return 0;
}
