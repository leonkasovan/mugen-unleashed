#include "mugen.h"
#include <SDL_mixer.h>
#include <assert.h>
#include <stdarg.h>
#include <libresample.h>
#include "soundcache.h"
/* Decode actual converted bank entries with real SDL2_mixer; no game window. */
int g_outOfMemory,g_streamError,g_sndResampleMethod=SND_RESAMPLE_LIBRESAMPLE;
AudioFormat g_sndFormat={44100,AUDIO_FMT_S16,2};
Resampler *g_sndResampler;
const ResamplerVtbl g_resamplerVtbl={Resampler_Open,Resampler_Create,Resampler_Close,Resampler_InputNeeded,Resampler_State,Resampler_Write,Resampler_Read,Resampler_InFormat,Resampler_OutFormat,Resampler_Reset,Resampler_InBlock,Resampler_OutBlock};
void MatchLog_Printf(int level,const char*fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
void Log_Printf(const char*fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
void *Mem_Calloc(size_t n,size_t sz){return calloc(n,sz);}
static unsigned int u32(FILE*f){unsigned char b[4];assert(fread(b,1,4,f)==4);return b[0]|((unsigned)b[1]<<8)|((unsigned)b[2]<<16)|((unsigned)b[3]<<24);}
int g_sndActive=1,g_sndSlotCount=256,g_sndMixChannels=16,g_sndMuted,g_stereoDirection=1;
SoundSlot *g_sndSlots;
static void put32(unsigned char *p,unsigned v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void test_growth(void){
 const unsigned bytes=768*1024;
 Snd *bank=Snd_Create(3);assert(bank);
 for(int i=0;i<3;i++){
  unsigned char *wave=calloc(1,bytes+44);assert(wave);
  memcpy(wave,"RIFF",4);put32(wave+4,bytes+36);memcpy(wave+8,"WAVEfmt ",8);
  put32(wave+16,16);wave[20]=1;wave[22]=2;put32(wave+24,44100);
  put32(wave+28,176400);wave[32]=4;wave[34]=16;memcpy(wave+36,"data",4);put32(wave+40,bytes);
  assert(Snd_Add(bank,90,i,NULL));assert(SoundCache_Add(bank,i,wave,bytes+44));
 }
 SndEntry *entries=(SndEntry*)bank->map->data;
 SoundCache_Configure(1,0,1,2,NULL,NULL);assert(SoundCache_Enabled());
 assert(SoundCache_Resolve(bank,0));assert(SoundCache_Resolve(bank,1));SoundCache_Tick();
 assert(!entries[0].chunk&&entries[1].chunk); /* fixed mode still evicts */
 SoundCache_Invalidate();SoundCache_Configure(1,1,1,2,NULL,NULL);
 assert(SoundCache_Resolve(bank,0));SoundCache_Tick();
 assert(SoundCache_Resolve(bank,1));SoundCache_Tick();
 assert(entries[0].chunk&&entries[1].chunk); /* growth retains 1.5 MiB */
 assert(SoundCache_Resolve(bank,2));SoundCache_Tick();
 assert(!entries[0].chunk&&entries[1].chunk&&entries[2].chunk); /* 2 MiB ceiling */
 SoundCache_Configure(1,1,0,2,NULL,NULL);SoundCache_Tick();
 for(int i=0;i<3;i++)assert(!entries[i].chunk); /* zero disables growth */
 Snd_Free(bank);
 SoundCache_Configure(0,0,0,20,NULL,NULL);assert(!SoundCache_Enabled());
 puts("PASS: fixed budget, automatic growth, ceiling eviction, zero budget");
}
int main(int argc,char **argv) {
 if(argc!=2){fprintf(stderr,"usage: %s bank.snd|bank.zip\n",argv[0]);return 2;}
 int q=0,status=0;
 SDL_setenv("SDL_AUDIODRIVER","dummy",1); SoundCache_Configure(0,0,0,20,NULL,NULL);
 assert(SDL_Init(SDL_INIT_AUDIO)==0);assert(Mix_OpenAudio(44100,AUDIO_S16SYS,2,2048)==0);Mix_AllocateChannels(16);
 g_sndSlots=calloc(256,sizeof(*g_sndSlots));for(int i=0;i<256;i++)g_sndSlots[i].channel=-1;
 resample_init(1,0);g_sndResampler=Resampler_Create(&g_sndFormat,2048,&q);
 test_growth();
 Stream *input=strstr(argv[1],".zip") ? Stream_OpenZip(argv[1],"bank.snd") : Stream_OpenFile(argv[1],"rb");assert(input);Snd *bank=Snd_ParseWithPolicy(input,&status,1);assert(bank&&status==0);Stream_Close(input);
 for(int i=0;i<bank->map->count;i++)assert(((SndEntry*)bank->map->data)[i].chunk==NULL);
 /* Compare every decoded clip against the eager path at the same format. */
 input=strstr(argv[1],".zip") ? Stream_OpenZip(argv[1],"bank.snd") : Stream_OpenFile(argv[1],"rb");assert(input);
 Snd *eager=Snd_Parse(input,&status);assert(eager&&status==0);Stream_Close(input);
 assert(eager->map->count==bank->map->count);
 for(int i=0;i<bank->map->count;i++){
  assert(SoundCache_Resolve(bank,i));
  Mix_Chunk *a=((SndEntry*)bank->map->data)[i].chunk,*b=((SndEntry*)eager->map->data)[i].chunk;
  assert(a&&b&&a->alen==b->alen&&!memcmp(a->abuf,b->abuf,a->alen));
  SoundCache_Tick();assert(((SndEntry*)bank->map->data)[i].chunk==NULL);
 }
 Snd_Free(eager);
 int group=bank->map->slots[0].key1,number=bank->map->slots[0].key2;
 char warm[64];snprintf(warm,sizeof(warm),"%d:%d",group,number);
 SoundCache_Configure(1,0,16,20,warm,NULL);
 SoundCache_WarmBank(bank);assert(((SndEntry*)bank->map->data)[0].chunk);
 SoundCache_Configure(1,0,0,20,NULL,NULL);SoundCache_Tick();
 assert(((SndEntry*)bank->map->data)[0].chunk==NULL);
 assert(Sound_PrepareSlot(0,bank,group,number)>=0);Mix_Chunk *chunk=g_sndSlots[0].chunk;assert(chunk);
 SoundCache_Tick();assert(g_sndSlots[0].chunk==chunk); /* prepared lease */
 Sound_PlaySlot(0);Sound_SetSlotLoop(0,1);Sound_PlaySlot(0);
 assert(Sound_PrepareSlot(1,bank,group,number)>=0);Sound_PlaySlot(1);
 assert(g_sndSlots[1].chunk==chunk);SoundCache_Tick();assert(g_sndSlots[0].chunk==chunk);
 Sound_Stop(-1);SoundCache_Tick();assert(((SndEntry*)bank->map->data)[0].chunk==NULL);
 assert(Sound_PrepareSlot(0,bank,group,number)>=0);SoundCache_Tick();assert(g_sndSlots[0].chunk);
 g_sndMixChannels=0;
 assert(Sound_PrepareSlot(9,bank,group,number)==-1 && g_sndSlots[9].chunk==NULL);
 g_sndMixChannels=16;
 SoundCache_Invalidate();assert(g_sndSlots[0].chunk==NULL);
 assert(Sound_PrepareSlot(0,bank,group,number)>=0); /* source survives format reset */
 Snd_Free(bank);assert(g_sndSlots[0].chunk==NULL);
 /* A corrupt encoded clip fails once, without changing a playing slot. */
 bank=Snd_Create(2);assert(bank);assert(Snd_Add(bank,77,0,NULL));
 unsigned char *bad=malloc(8);memcpy(bad,"OggSbad",8);
 assert(SoundCache_Add(bank,0,bad,8));
 assert(!SoundCache_Resolve(bank,0));assert(!SoundCache_Resolve(bank,0));
 unsigned char *wave=calloc(1,60);memcpy(wave,"RIFF",4);memcpy(wave+8,"WAVEfmt ",8);memcpy(wave+36,"data",4);
 unsigned v=52;memcpy(wave+4,&v,4);v=16;memcpy(wave+16,&v,4);memcpy(wave+40,&v,4);
 wave[20]=1;wave[22]=2;v=44100;memcpy(wave+24,&v,4);v=176400;memcpy(wave+28,&v,4);wave[32]=4;wave[34]=16;
 assert(Snd_Add(bank,77,0,NULL));assert(SoundCache_Add(bank,1,wave,60));
 assert(Sound_PlayOnSlot(0,bank,77,0,100)>=0); /* first bad duplicate must not mask the valid one */
 assert(g_sndSlots[0].chunk==((SndEntry*)bank->map->data)[1].chunk);
 Snd_Free(bank);
 /* Metadata extents are checked before any decoder is called. */
 unsigned char header[528]={0};memcpy(header,"ElecbyteSnd",12);
 unsigned count=2,first=512,next=512,zero=0;
 memcpy(header+16,&count,4);memcpy(header+20,&first,4);memcpy(header+512,&next,4);memcpy(header+516,&zero,4);
 input=Stream_OpenMemory(header,sizeof(header));assert(input);
 assert(Snd_ParseWithPolicy(input,&status,1)==NULL && status==SND_ERR_FAILED);Stream_Close(input);
 SoundCache_Reset();free(g_sndSlots);
 free(g_sndResampler->quality);free(g_sndResampler);resample_deinit();Mix_CloseAudio();Mix_Quit();SDL_Quit();
 puts("PASS: eager PCM equality, owned source, prepared/overlap/loop leases, eviction, re-decode, bank teardown");return 0;
}
