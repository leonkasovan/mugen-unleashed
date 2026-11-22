/* Regression: real lazy codecs, stream cleanup and SFF admission policy. */
#include "mugen.h"
#include <assert.h>
#undef main
#include "../src/sff.c"
int g_outOfMemory, g_renderMode=RENDER_SYSTEM, g_streamError;
Stream *g_pngStream;
DecompBuffer *g_spriteDecompBuffer;
SpriteDecodeFn g_spriteDecoders[14]={[2]=(SpriteDecodeFn)Rle8_Decode,[12]=(SpriteDecodeFn)Png_Decode,[13]=(SpriteDecodeFn)Lz4_Decode};
static int fail_png_alloc, closes;
static void *stream_alloc;
void *Mem_Calloc(size_t n,size_t s){return stream_alloc=calloc(n,s);}
void *Memprof_Malloc(size_t n){return fail_png_alloc?NULL:malloc(n);}
void Memprof_Free(void *p){free(p);}
void __real_free(void *p);
void __wrap_free(void *p){if(p && p==stream_alloc){closes++;stream_alloc=NULL;}__real_free(p);}
int unzClose(unzFile f){return UNZ_OK;}
int unzCloseCurrentFile(unzFile f){return UNZ_OK;}
void MatchLog_Printf(int level,const char *fmt,...){}
int main(void){
 unsigned char png[]={4,0,0,0,137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,16,84,50,254,15,0,2,20,1,102,4,224,222,127,0,0,0,0,73,69,78,68,174,66,96,130};
 unsigned char tiny[]={1,0,0,0,7}, decoded[4]={0};
 void *out=NULL;
 Sff2SpriteNode node={0};
 g_spriteDecompBuffer=DecompBuffer_Create(128*1024);assert(g_spriteDecompBuffer);
 assert(SpriteCache_Acquire(tiny,2,sizeof(tiny),1,&out));
 assert(!SpriteCache_Acquire(tiny,2,sizeof(tiny),262144,&out));
 assert(!SpriteCache_Acquire(tiny,3,sizeof(tiny),1,&out));
 assert(!SpriteCache_Acquire(tiny,2,4,1,&out));
 assert(Png_Decode(png,sizeof(png),decoded));assert(!memcmp(decoded,"\x11\x22\x33\xff",4));
 assert(SpriteCache_Acquire(png,12,sizeof(png),4,&out));assert(!memcmp(out,decoded,4));
 fail_png_alloc=1;closes=0;
 assert(!Png_Decode(png,sizeof(png),decoded));assert(closes==1);
 node.w=node.h=512;node.depth=8;assert(Sff_DecodeOnLoad(&node));
 {
  unsigned remaining=512*512, at=4;
  unsigned char *encoded=malloc(4+2*((remaining+62)/63));
  Sprite spr;KeyedArray array={0};SpriteSet set={0};
  assert(encoded);encoded[0]=encoded[1]=encoded[3]=0;encoded[2]=4;
  while(remaining){unsigned n=remaining>63?63:remaining;encoded[at++]=0x40|n;encoded[at++]=7;remaining-=n;}
  assert(Sprite_Init((Sff_DecodeOnLoad(&node)?SPRITE_INIT_DECODE:0)|SPRITE_INIT_NO_TEXTURE,
                    &spr,2,encoded,at,512,512,8,0,0));
  array.data=(void *)&spr;array.elem_size=sizeof(spr);set.sprites=&array;
  assert(spr.format==SPRITE_FORMAT_RAW && Sprite_GetPixels(&set,0));
  assert(((unsigned char *)Sprite_GetPixels(&set,0))[512*512-1]==7);
  Sprite_Free(&spr);free(encoded);
 }
 node.w=node.h=1;assert(!Sff_DecodeOnLoad(&node));
 free(g_spriteDecompBuffer->data);free(g_spriteDecompBuffer);
 puts("ok: PNG success/failure cleanup, cache metadata, oversized SFF admission");return 0;
}
