#define _GNU_SOURCE
#include "mugen.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
/* Exercise the SFF record-bound calculation together with the real decoder. */
#include "../src/sff.c"
int g_outOfMemory, g_streamError;
static unsigned char captured[1024];
static int fail_palette;
static size_t requested;
void *__real_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size) {
    requested = n * size;
    if (size && n > (16u * 1024u * 1024u) / size) {errno = ENOMEM; return NULL;}
    return __real_calloc(n, size);
}
void *Mem_Calloc(size_t n, size_t size) {return calloc(n, size);}
int Pal_Init(Palette *p, int n, void *colors, int flags) {
    assert(n == 256); memcpy(captured, colors, 1024); return !fail_palette;
}
static void u16(unsigned char *p, unsigned value) {p[0] = value; p[1] = value >> 8;}
static void header(unsigned char *b, unsigned w, unsigned h, unsigned planes, unsigned stride) {
    memset(b, 0, 128); b[0] = 10; b[1] = 5; b[2] = 1; b[3] = 8;
    u16(b + 8, w - 1); u16(b + 10, h - 1); b[65] = planes; u16(b + 66, stride);
}
static void decode(const char *name, unsigned char *b, size_t length, int palette) {
    Stream s = {.type = STREAM_MEMORY, .begin=b, .pos=b, .end=b+length};
    int w,h,bpp; size_t size;
    memset(captured,0,sizeof(captured)); requested=0;
    unsigned char *p = Pcx_Decode(palette ? &(Palette){0} : NULL,&w,&s,&h,&bpp,&size,0);
    printf("%s: accepted=%d w=%d h=%d bpp=%d size=%zu requested=%zu first=",name,p!=NULL,w,h,bpp,size,requested);
    if (p) for(size_t i=0;i<size && i<12;i++)printf("%02x",p[i]);
    printf(" pal0=%02x%02x%02x%02x pos=%ld\n",captured[0],captured[1],captured[2],captured[3],Stream_Tell(&s));
    if (!strcmp(name,"valid indexed")) assert(p && size==4 && !memcmp(p,"\1\2\3\4",4));
    else if (!strcmp(name,"valid RGB padded")) assert(p && size==3 && p[0]==0x33 && p[1]==0x22 && p[2]==0x11);
    else if (!strcmp(name,"valid unsigned coords crossing 32767")) assert(p && w==2);
    else if (!strcmp(name,"no palette standalone") || !strcmp(name,"same PCX with following record")) assert(p);
    else assert(!p && w==0 && h==0 && bpp==0 && size==0);
    if (!strcmp(name,"invalid unsigned Y extents")) assert(requested==0);
    free(p);
}
typedef struct Cookie {unsigned char *data; size_t pos,len; int reported;} Cookie;
static ssize_t cookie_read(void *arg, char *buf, size_t n) {
    Cookie *c=arg;
    if(c->pos>=130) {if(!c->reported){fprintf(stderr,"injected EIO after image; palette scan should terminate\n");c->reported=1;}errno=EIO;return -1;}
    if(n>130-c->pos)n=130-c->pos;
    memcpy(buf,c->data+c->pos,n);c->pos+=n;return n;
}
static int cookie_seek(void *arg,off64_t *offset,int whence) {
    Cookie *c=arg; off64_t n=*offset+(whence==SEEK_CUR?(off64_t)c->pos:whence==SEEK_END?(off64_t)c->len:0);
    if(n<0||(uint64_t)n>c->len){errno=EINVAL;return -1;}c->pos=n;*offset=n;return 0;
}
int main(int argc, char **argv) {
    unsigned char b[1024]={0};
    if(argc>1 && !strcmp(argv[1],"io-error")) {
        header(b,1,1,1,2);b[128]=1;b[129]=0;
        Cookie c={.data=b,.len=901};cookie_io_functions_t io={.read=cookie_read,.seek=cookie_seek};
        FILE *fp=fopencookie(&c,"rb",io);assert(fp);Stream s={.type=STREAM_FILE,.fp=fp};
        int w,h,bpp;size_t n;Palette pal;
        void *p=Pcx_Decode(&pal,&w,&s,&h,&bpp,&n,0);
        assert(!p);assert(w==0 && h==0 && bpp==0 && n==0);printf("PASS: I/O error rejected\n");free(p);fclose(fp);return 0;
    }
    header(b,2,2,1,2);b[128]=1;b[129]=2;b[130]=3;b[131]=4;
    decode("valid indexed",b,132,0);
    header(b,1,1,3,2);unsigned char rgb[]={0x11,0,0x22,0,0x33,0};memcpy(b+128,rgb,6);
    decode("valid RGB padded",b,134,0);
    header(b,2,2,1,2);decode("truncated header",b,68,0);
    decode("missing pixels",b,128,0);
    b[128]=0xc4;decode("truncated RLE value",b,129,0);
    header(b,2,1,1,2);b[128]=1;b[129]=2;b[130]=12;
    decode("truncated palette",b,131,1);
    header(b,2,1,1,2);b[128]=1;b[129]=2;
    decode("no palette standalone",b,130,1);
    b[130]=0x99;b[131]=0x99;b[132]=12;memset(b+133,0x2a,768);
    decode("same PCX with following record",b,901,1);
    header(b,2,1,1,2);u16(b+4,32767);u16(b+8,32768);b[128]=1;b[129]=2;
    decode("valid unsigned coords crossing 32767",b,130,0);
    header(b,2,1,1,2);b[2]=0;b[128]=0xc1;b[129]=0x02;
    decode("uncompressed encoding",b,130,0);
    header(b,32767,1,1,32767);u16(b+6,0x8000);u16(b+10,0x7fff);
    decode("invalid unsigned Y extents",b,128,0);
    header(b,2,1,1,2);b[128]=1;b[129]=2;b[130]=12;memset(b+131,0x44,768);
    Stream bounded={.type=STREAM_MEMORY,.begin=b,.pos=b,.end=b+899};
    int w,h,depth;size_t size;Palette pal;
    void *p=Pcx_DecodeSized(&pal,&w,&bounded,&h,&depth,&size,0,130);
    assert(p && captured[0]==0 && captured[3]==0 && Stream_Tell(&bounded)==0);free(p);
    b[128]=0xc0;b[129]=1;
    p=Pcx_DecodeSized(NULL,&w,&bounded,&h,&depth,&size,0,130);assert(!p);
    b[128]=0xc3;
    p=Pcx_DecodeSized(NULL,&w,&bounded,&h,&depth,&size,0,130);assert(!p);
    {
        unsigned char file[1024] = {0};
        const long prefix = 17, start = prefix + SFF_HEADER_SIZE;
        const long pcx = start + 32, next = pcx + 130;
        Stream record = {.type=STREAM_MEMORY, .begin=file, .pos=file+pcx, .end=file+next};
        Sff1SubHeader sub = {.size=130+768, .same_palette=1};
        size_t length;

        header(file+pcx,2,1,1,2); file[pcx+128]=1; file[pcx+129]=2;
        /* Last shared-palette record: declared size extends beyond EOF. */
        length=Sff1_PcxLength(&sub,&record,start); assert(length==130);
        p=Pcx_DecodeSized(NULL,&w,&record,&h,&depth,&size,0,length);
        assert(p && size==2 && !memcmp(p,"\1\2",2)); free(p);
        /* Non-final record, embedded SFF: next_offset is SFF-relative. */
        record.end=file+sizeof(file); sub.next_offset=(unsigned)(next-prefix);
        file[next]=12; memset(file+next+1,0x44,sizeof(file)-next-1);
        length=Sff1_PcxLength(&sub,&record,start); assert(length==130);
        p=Pcx_DecodeSized(&pal,&w,&record,&h,&depth,&size,0,length);
        assert(p && captured[0]==0); free(p); /* adjacent palette marker is ignored */
        sub.size=129; assert(Sff1_PcxLength(&sub,&record,start)==129);
        p=Pcx_DecodeSized(NULL,&w,&record,&h,&depth,&size,0,Sff1_PcxLength(&sub,&record,start));
        assert(!p); /* A short declared size still truncates the image. */
        sub.size=898; sub.next_offset=(unsigned)(next-prefix-1);
        p=Pcx_DecodeSized(NULL,&w,&record,&h,&depth,&size,0,Sff1_PcxLength(&sub,&record,start));
        assert(!p); /* A following record cannot supply the missing pixel. */
        sub.size=898; sub.next_offset=(unsigned)(pcx-prefix-1);
        assert(Sff1_PcxLength(&sub,&record,start)==0);
        sub.next_offset=sizeof(file); assert(Sff1_PcxLength(&sub,&record,start)==0);
        sub.next_offset=(unsigned)(next-prefix); sub.size=130;
        file[pcx+128]=0xc3; /* RLE run crosses the scanline: still rejected. */
        p=Pcx_DecodeSized(NULL,&w,&record,&h,&depth,&size,0,Sff1_PcxLength(&sub,&record,start));
        assert(!p);
        puts("PASS: shared-palette SFF v1 EOF/next-record bounds and embedded offsets");
    }
    puts("PASS: PCX bounds, encoding, truncation, unsigned coordinates and record isolation");
    return 0;
}
