/* Regression checks for asset decoder bounds and failure cleanup. */
#include "mugen.h"
#include <SDL_mixer.h>
#include <assert.h>
#undef malloc
#undef calloc
#undef realloc
#undef free
static void *ptrs[64]; static size_t sizes[64];
static int fail_realloc, fail_chunk, null_session, consumed;
int g_outOfMemory, g_renderMode=1, g_glMaxTextureSize=4096;
AudioFormat g_sndFormat={44100,AUDIO_FMT_S16,2};
Resampler *g_sndResampler;
SpriteDecodeFn g_spriteDecoders[13];
static void remember(void *p,size_t n) { if(!p)return;for(int i=0;i<64;i++)if(!ptrs[i]){ptrs[i]=p;sizes[i]=n;return;}abort(); }
void *Memprof_Malloc(size_t n){void*p=malloc(n);remember(p,n);return p;}
void *Memprof_Calloc(size_t n,size_t s){void*p=calloc(n,s);remember(p,n*s);return p;}
void Memprof_Free(void*p){for(int i=0;i<64;i++)if(ptrs[i]==p){ptrs[i]=NULL;sizes[i]=0;break;}free(p);}
void *Memprof_Realloc(void*p,size_t n){if(fail_realloc)return NULL;for(int i=0;i<64;i++)if(ptrs[i]==p){ptrs[i]=NULL;sizes[i]=0;break;}void*q=realloc(p,n);remember(q,n);return q;}
static size_t live(void){size_t n=0;for(int i=0;i<64;i++)n+=sizes[i];return n;}
void MatchLog_Printf(int level,const char*fmt,...){}
long Stream_Tell(Stream*s){return s->pos-s->begin;}
int Stream_Seek(Stream*s,long n,int origin){s->pos=(origin==SEEK_SET?s->begin:origin==SEEK_END?s->end:s->pos)+n;s->eof=0;return 0;}
int Stream_GetByte(Stream*s){if(s->pos>=s->end){s->eof=1;return -1;}return *s->pos++;}
int Stream_ReadU16(Stream*s,unsigned short*v){int a=Stream_GetByte(s),b=Stream_GetByte(s);*v=a|(b<<8);return a<0||b<0?-1:0;}
int Stream_ReadU32(Stream*s,unsigned int*v){unsigned short a,b;int e=Stream_ReadU16(s,&a)|Stream_ReadU16(s,&b);*v=a|((unsigned)b<<16);return e;}
int Pal_Init(Palette*p,int n,void*c,int f){return 0;}
GlTex *GlTex_Create(void){return NULL;}
int GlTex_Free(GlTex*t){return 0;}
int GlTex_Upload2D(int h,int bpp,GlTex*t,const void*p,int w){return 0;}
void SpriteCache_Remove(void*p){}
void SDL_FreeSurface(SDL_Surface*p){}
int Wav_FrameSize(int format,int channels){return channels*2;}
int Wav_ReadInfo(Stream*s,WavInfo*i){memset(i,0,sizeof(*i));i->format=g_sndFormat;i->frames=4;i->format_tag=WAV_TAG_PCM;return 0;}
WavReader *WavReader_Open(const WavInfo*i,Stream*s,int*status){WavReader*r=Memprof_Calloc(1,sizeof(*r));r->info=*i;return r;}
unsigned int WavReader_Read(WavReader*r,unsigned int f,void*buf){return 0;}
static const ResamplerVtbl methods;
static ResamplerSession sess={NULL,&methods};
static ResamplerSession *begin(Resampler*r,const WavInfo*i){return null_session?NULL:&sess;}
static unsigned int block(ResamplerSession*s){return 4;}
static int state(ResamplerSession*s){return consumed?RESAMPLER_DONE:RESAMPLER_OUTPUT;}
static unsigned int read_samples(ResamplerSession*s,void*p,unsigned int n){consumed=1;memset(p,0,16);return 4;}
static void close_samples(ResamplerSession*s){}
static const ResamplerVtbl methods={.in_block=block,.state=state,.read=read_samples,.close=close_samples};
static Resampler converter={.begin=begin};
Mix_Chunk *Mix_QuickLoad_RAW(Uint8*p,Uint32 n){static Mix_Chunk c;if(fail_chunk)return NULL;c.abuf=p;c.alen=n;return &c;}
int main(int argc,char**argv){
 unsigned char input[1024]={0};Stream stream={.type=STREAM_MEMORY,.begin=input,.pos=input,.end=input+sizeof(input)};
 if(!strcmp(argv[1],"valid-codecs")){
  unsigned char rle8[]={4,0,0,0,1,2,3,4};
  unsigned char rle5[]={4,0,0,0,3,0};
  unsigned char lz5[]={4,0,0,0,0,129};
  unsigned char bad_lz[]={4,0,0,0,1,1,0};
  unsigned char truncated_run[]={4,0,0,0,0x44};
  Sprite spr;
  assert(Sprite_ValidateCompressed(rle8,sizeof(rle8),2,4));
  assert(Sprite_ValidateCompressed(rle5,sizeof(rle5),3,4));
  assert(Sprite_ValidateCompressed(lz5,sizeof(lz5),4,4));
  assert(!Sprite_ValidateCompressed(bad_lz,sizeof(bad_lz),4,4));
  assert(!Sprite_ValidateCompressed(truncated_run,sizeof(truncated_run),2,4));
  g_spriteDecoders[2]=Rle8_Decode;
  assert(Sprite_Init(SPRITE_INIT_DECODE,&spr,2,rle8,sizeof(rle8),2,2,8,0,0));
  assert(!memcmp(spr.pixels,rle8+4,4));
  Sprite_Free(&spr);
 }else if(!strcmp(argv[1],"pcx-palette")){
  input[3]=8;input[8]=1;input[10]=1;input[65]=1;input[66]=2;input[132]=12;
  Palette pal;int w,h,bpp;size_t n;void*p=Pcx_Decode(&pal,&w,&stream,&h,&bpp,&n,0);
  assert(p == NULL && live() == 0);
 }else if(!strcmp(argv[1],"pcx-overflow")){
  input[3]=8;input[4]=0;input[5]=128;input[8]=255;input[9]=127;input[10]=85;input[11]=85;input[65]=3;input[66]=1;
  int w,h,bpp;size_t n;assert(Pcx_Decode(NULL,&w,&stream,&h,&bpp,&n,0) == NULL);
 }else if(!strcmp(argv[1],"sprite-rle-overflow")){
  unsigned char data[6]={4,0,0,0,0x7f,1};Sprite spr;g_spriteDecoders[2]=Rle8_Decode;
  assert(Sprite_Init(SPRITE_INIT_DECODE,&spr,2,data,sizeof(data),2,2,8,0,0) == 0);
 }else if(!strcmp(argv[1],"sprite-short")){
  Sprite spr;unsigned char*p=Memprof_Malloc(2);assert(Sprite_Init(SPRITE_INIT_DECODE|SPRITE_INIT_OWN,&spr,2,p,2,2,2,8,0,0) == 0);
 }else{
  g_sndResampler=&converter;null_session=!strcmp(argv[1],"sound-null");fail_realloc=!strcmp(argv[1],"sound-realloc");fail_chunk=!strcmp(argv[1],"sound-chunk");int status=0;
  Mix_Chunk*c=Snd_DecodeWavResampled(&stream,&status);
  assert(c == NULL && live() == 0);
  if(c)Memprof_Free(c->abuf);
 }
 assert(live() == 0);
 /* No orphan allocations are permitted after a rejected asset. */
 return 0;
}
