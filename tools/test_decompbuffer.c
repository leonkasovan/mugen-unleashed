/* Regression tests: bounded pool sizing, OOM cleanup and stable cache metadata. */
#include "mugen.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
int g_outOfMemory;
static int fail_malloc, fail_calloc;
/* Substitute only allocator calls in the actual implementation, so fault
 * injection works with both ELF and MinGW CRT imports. */
static void *test_malloc(size_t n) {return fail_malloc ? NULL : malloc(n);}
static void *test_calloc(size_t n,size_t z) {return fail_calloc ? NULL : calloc(n,z);}
#define malloc test_malloc
#define calloc test_calloc
#include "../src/spritecodec.c"
#undef malloc
#undef calloc
static void destroy(DecompBuffer *b) {if(b){free(b->data);free(b);}}
static void verify(DecompBuffer *b) {
    unsigned char *p=b->data,*end=p+b->size-sizeof(DecompBlock);
    unsigned used=0,free_count=0,blocks=0,bytes=0;int prev_free=0;
    while(p<end){
        DecompBlock *h=(void*)p;
        assert((uintptr_t)p%_Alignof(DecompBlock)==0);
        assert(h->size>=sizeof(*h) && h->size<=(size_t)(end-p));
        assert(h->size%_Alignof(DecompBlock)==0);
        ++blocks;
        if(h->key){++used;bytes+=h->size;prev_free=0;}
        else {++free_count;assert(!prev_free);prev_free=1;}
        p+=h->size;
    }
    assert(p==end && ((DecompBlock*)p)->size==0);
    assert(used==(unsigned)b->used_count && free_count==(unsigned)b->free_count);
    assert(blocks==(unsigned)b->block_count && bytes==(unsigned)b->used_bytes);
}
int main(int argc,char **argv) {
    assert(argc==2);setvbuf(stdout,NULL,_IONBF,0);
    if(!strcmp(argv[1],"zero") || !strcmp(argv[1],"upper")) {
        unsigned n=!strcmp(argv[1],"zero")?0:UINT_MAX;
        DecompBuffer *b=DecompBuffer_Create(n);assert(!b);puts("PASS: invalid constructor size rejected");return 0;
    }
    if(!strcmp(argv[1],"config")) {
        assert(DecompBuffer_ConfigBytes(4194304)==0);
        assert(DecompBuffer_ConfigBytes(2097152)==0);
        assert(DecompBuffer_ConfigBytes(INT_MAX)==0);
        assert(DecompBuffer_ConfigBytes(-1)==0);
        assert(DecompBuffer_ConfigBytes(0)==0);
        assert(DecompBuffer_ConfigBytes(127)==0);
        assert(DecompBuffer_ConfigBytes(128)==131072);
        assert(DecompBuffer_ConfigBytes(16384)==16*1024*1024);
        assert(DecompBuffer_ConfigBytes(DECOMP_MAX_KIB)==DECOMP_MAX_BYTES);
        assert(!DecompBuffer_Create(DECOMP_MAX_BYTES+1));
        DecompBuffer*b=DecompBuffer_Create(1);assert(b && b->size==1024);verify(b);destroy(b);
        b=DecompBuffer_Create(1025);assert(b && b->size==2048);verify(b);destroy(b);
        fail_malloc=1;g_outOfMemory=0;b=DecompBuffer_Create(DECOMP_MAX_BYTES);
        assert(!b && g_outOfMemory); /* Largest accepted request; never actually allocated. */
        puts("PASS: checked KiB conversion, constructor rounding and maximum boundary");return 0;
    }
    if(!strcmp(argv[1],"oom")) {
        fail_calloc=1;DecompBuffer*b=DecompBuffer_Create(1024);
        assert(!b);printf("header calloc failure: outOfMemory=%d\n",g_outOfMemory);
        fail_calloc=0;g_outOfMemory=0;fail_malloc=1;
        b=DecompBuffer_Create(1024);assert(!b);
        assert(g_outOfMemory);puts("PASS: payload OOM reported and cleaned up");return 0;
    }
    DecompBuffer *b=DecompBuffer_Create(!strcmp(argv[1],"clock")?16*1024*1024:64*1024);assert(b);verify(b);
    unsigned char keys[128];
    if(!strcmp(argv[1],"request")) {
        unsigned sizes[]={0,UINT_MAX,UINT_MAX-1,INT_MAX,b->size,b->size-2*sizeof(DecompBlock)+1};
        for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
            DecompRequest r={.pool=b,.size=sizes[i],.key=keys,.cost=1};int hit=9;
            assert(!DecompBuffer_Get(&r,&hit) && !hit);
            assert(!DecompBuffer_Alloc(&r));verify(b);
        }
        int hit=9;
        assert(!DecompBuffer_Get(NULL,&hit) && !hit);
        assert(!DecompBuffer_Alloc(NULL));assert(!DecompBuffer_Reset(NULL));
        DecompRequest r={.pool=b,.size=8,.key=keys,.cost=-1};
        assert(!DecompBuffer_Get(&r,&hit));assert(!DecompBuffer_Alloc(&r));verify(b);
        puts("PASS: empty/oversized requests, negative costs and invalid arguments rejected");
    } else if(!strcmp(argv[1],"null-key")) {
        DecompRequest r={.pool=b,.size=64,.key=NULL,.cost=1};int hit;
        void*p=DecompBuffer_Get(&r,&hit);
        assert(!p && !hit && !b->used_count && b->free_count==1);assert(!DecompBuffer_Alloc(&r));verify(b);puts("PASS: NULL key cannot match a free block");
    } else if(!strcmp(argv[1],"clock")) {
        for(int i=0;i<6000;i++) {
            DecompRequest r={.pool=b,.size=4095*4096,.key=keys+(i%2),.cost=1024*1024};int hit;

            assert(DecompBuffer_Get(&r,&hit) && !hit);verify(b);
            assert(b->clock>=0 && ((DecompBlock*)b->data)->expire>=0);
        }
        destroy(b);b=DecompBuffer_Create(65536);assert(b);
        for(int i=0;i<2;i++) {
            DecompRequest r={.pool=b,.size=30000,.key=keys+i,.cost=INT_MAX};int hit;
            assert(DecompBuffer_Get(&r,&hit) && !hit);verify(b);
        }
        DecompRequest r={.pool=b,.size=50000,.key=keys+2,.cost=INT_MAX};int hit;
        unsigned char*p=DecompBuffer_Get(&r,&hit);assert(p && !hit);memset(p,0x3a,50000);verify(b);
        assert(b->clock==INT_MAX && ((DecompBlock*)p-1)->expire==INT_MAX);
        b->hits=b->misses=b->evictions=INT_MAX;
        assert(DecompBuffer_Get(&r,&hit)==p && hit && p[49999]==0x3a);
        r.key=keys+3;assert(DecompBuffer_Get(&r,&hit) && !hit);verify(b);
        assert(b->hits==INT_MAX && b->misses==INT_MAX && b->evictions==INT_MAX);
        assert(!DecompBuffer_Get(&r,NULL));
        puts("PASS: 6000 large-image misses, wide multi-block sums, saturated hits/eviction/counters");
    } else {
        unsigned rng=12345;
        for(int i=0;i<20000;i++) {
            rng=rng*1664525u+1013904223u;
            unsigned n=1+(rng%8192);
            DecompRequest r={.pool=b,.size=n,.key=keys+(rng>>24)%128,.cost=1+(int)(n/4)};int hit;
            void*p=DecompBuffer_Get(&r,&hit);assert(p);
            assert(((DecompBlock*)p-1)->size>=sizeof(DecompBlock)+r.size);
            memset(p,0x5a,n);verify(b);
            if(i%17==0){DecompBlock*prev=NULL,*h=b->data;while(h->size){if(h->key==r.key){DecompBuffer_FreeBlock(h,b,prev);break;}prev=h;h=(void*)((char*)h+h->size);}verify(b);}
            if(i%1000==999){DecompBuffer_Reset(b);verify(b);}
        }
        puts("PASS: 20000 mixed-size requests, hits, evictions, free/coalescing, reset and alignment");
    }
    destroy(b);return 0;
}
