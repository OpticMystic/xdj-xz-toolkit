/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "overcue.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define JSMN_STATIC
#include "vendor/jsmn/jsmn.h"
#include "vendor/sha256/sha256.h"
#include "vendor/miniz/miniz_tinfl.h"

struct document { char *text; jsmntok_t *token; int count; };
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p)<<32 | be32(p+4); }
static int hex(unsigned char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static int digest_matches(const unsigned char digest[32], const char *hexstr) {
    if (strlen(hexstr)!=64) return 0;
    for (unsigned i=0;i<32;i++) if (hex((unsigned char)hexstr[i*2])<0 || hex((unsigned char)hexstr[i*2+1])<0 ||
        digest[i] != (unsigned)(hex((unsigned char)hexstr[i*2])*16+hex((unsigned char)hexstr[i*2+1]))) return 0;
    return 1;
}
static void hash(const void *data,size_t n,unsigned char out[32]) { SHA256_CTX c;sha256_init(&c);sha256_update(&c,data,n);sha256_final(&c,out); }
static int file_hash_matches(const char *path,const char *expected) {
    FILE *f=fopen(path,"rb"); if(!f)return 0;
    SHA256_CTX c;sha256_init(&c);unsigned char data[32768],sum[32];size_t n;uint64_t total=0;
    while((n=fread(data,1,sizeof(data),f))){sha256_update(&c,data,n);total+=n;if(total>UINT64_C(2147483648)){fclose(f);return 0;}}
    int ok=!ferror(f);fclose(f);sha256_final(&c,sum);return ok&&digest_matches(sum,expected);
}
static int document_open(const char *path,struct document *d) {
    memset(d,0,sizeof(*d));FILE *f=fopen(path,"rb");if(!f)return -1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -1;}long size=ftell(f);rewind(f);
    if(size<2||size>4*1024*1024){fclose(f);return -1;}
    d->text=calloc((size_t)size+1,1);d->token=calloc((size_t)size/2+1,sizeof(*d->token));
    if(!d->text||!d->token){fclose(f);return -1;}
    int ok=fread(d->text,1,(size_t)size,f)==(size_t)size;fclose(f);if(!ok)return -1;
    jsmn_parser parser;jsmn_init(&parser);
    d->count=jsmn_parse(&parser,d->text,(size_t)size,d->token,(unsigned)size/2+1);
    return d->count>0&&d->token[0].type==JSMN_OBJECT?0:-1;
}
static void document_close(struct document *d){free(d->text);free(d->token);memset(d,0,sizeof(*d));}
static int equal(const struct document *d,int t,const char *s){return t>=0&&t<d->count&&(size_t)(d->token[t].end-d->token[t].start)==strlen(s)&&!memcmp(d->text+d->token[t].start,s,strlen(s));}
static int next(const struct document *d,int t){int end=d->token[t].end;while(++t<d->count&&d->token[t].start<end){}return t;}
static int field(const struct document *d,int object,const char *name){
    if(object<0||object>=d->count||d->token[object].type!=JSMN_OBJECT)return -1;
    for(int t=object+1;t<d->count&&d->token[t].start<d->token[object].end;){
        int value=t+1;if(value>=d->count)return -1;
        if(equal(d,t,name))return value;t=next(d,value);
    }return -1;
}
static int string(const struct document *d,int t,char *out,size_t cap){
    if(t<0||t>=d->count||d->token[t].type!=JSMN_STRING||!cap)return -1;
    size_t n=0;int end=d->token[t].end;
    for(int p=d->token[t].start;p<end;){
        unsigned c=(unsigned char)d->text[p++];
        if(c=='\\'){
            if(p>=end)return -1;c=(unsigned char)d->text[p++];
            if(c=='u'){
                if(p+4>end)return -1;c=0;
                for(int k=0;k<4;k++){int h=hex((unsigned char)d->text[p++]);if(h<0)return -1;c=c*16+(unsigned)h;}
                if(c>=0xd800&&c<=0xdbff){
                    if(p+6>end||d->text[p++]!='\\'||d->text[p++]!='u')return -1;unsigned low=0;
                    for(int k=0;k<4;k++){int h=hex((unsigned char)d->text[p++]);if(h<0)return -1;low=low*16+(unsigned)h;}
                    if(low<0xdc00||low>0xdfff)return -1;c=0x10000+(c-0xd800)*1024+low-0xdc00;
                }else if(c>=0xdc00&&c<=0xdfff)return -1;
                unsigned char utf[4];size_t count;
                if(c<0x80){utf[0]=(unsigned char)c;count=1;}
                else if(c<0x800){utf[0]=(unsigned char)(0xc0|(c>>6));utf[1]=(unsigned char)(0x80|(c&63));count=2;}
                else if(c<0x10000){utf[0]=(unsigned char)(0xe0|(c>>12));utf[1]=(unsigned char)(0x80|((c>>6)&63));utf[2]=(unsigned char)(0x80|(c&63));count=3;}
                else{utf[0]=(unsigned char)(0xf0|(c>>18));utf[1]=(unsigned char)(0x80|((c>>12)&63));utf[2]=(unsigned char)(0x80|((c>>6)&63));utf[3]=(unsigned char)(0x80|(c&63));count=4;}
                if(!c||n+count>=cap)return -1;memcpy(out+n,utf,count);n+=count;continue;
            }
            if(c!='/'&&c!='\\'&&c!='"')return -1;
        }
        if(c<32||n+1>=cap)return -1;out[n++]=(char)c;
    }out[n]=0;return 0;
}
static double number(const struct document *d,int t){
    if(t<0||t>=d->count||d->token[t].type!=JSMN_PRIMITIVE)return -1;
    char *end;double n=strtod(d->text+d->token[t].start,&end);
    return end==d->text+d->token[t].end&&isfinite(n)?n:-1;
}
static int path_join(char out[4096],const char *a,const char *b){return snprintf(out,4096,"%s/%s",a,b)>=4096?-1:0;}
static int open_pgz(const char *path,const char *table_sha,uint64_t frames,struct xz_oc_file *f){
    unsigned char header[24],sum[32];for(unsigned i=0;i<XZ_OC_PAGE_SLOTS;i++)f->cached_page[i]=UINT32_MAX;
    f->file=fopen(path,"rb");if(!f->file)return -1;
    if(fread(header,1,24,f->file)!=24||memcmp(header,"OVPGZ001",8)||be32(header+8)!=XZ_OC_PAGE)return -1;
    f->page_count=be32(header+12);f->bytes=be64(header+16);
    if(f->bytes!=frames*4||!f->page_count||f->page_count>65536||f->page_count!=(f->bytes+XZ_OC_PAGE-1)/XZ_OC_PAGE)return -1;
    size_t table_bytes=(size_t)f->page_count*48;unsigned char *table=malloc(table_bytes);if(!table)return -1;
    if(fread(table,1,table_bytes,f->file)!=table_bytes){free(table);return -1;}
    SHA256_CTX ctx;sha256_init(&ctx);sha256_update(&ctx,header,24);sha256_update(&ctx,table,table_bytes);sha256_final(&ctx,sum);
    if(!digest_matches(sum,table_sha)){free(table);return -1;}
    if(fseek(f->file,0,SEEK_END)){free(table);return -1;}long file_bytes=ftell(f->file);if(file_bytes<0){free(table);return -1;}
    f->pages=calloc(f->page_count,sizeof(*f->pages));f->raw=malloc(XZ_OC_PAGE*XZ_OC_PAGE_SLOTS);f->compressed=malloc(XZ_OC_PAGE+4096);
    if(!f->pages||!f->raw||!f->compressed){free(table);return -1;}
    uint64_t previous=24+table_bytes,total=0;
    for(uint32_t i=0;i<f->page_count;i++){
        const unsigned char *p=table+(size_t)i*48;struct xz_oc_page *page=f->pages+i;
        page->offset=be64(p);page->compressed=be32(p+8);page->bytes=be32(p+12);memcpy(page->sha,p+16,32);
        if(page->offset!=previous||!page->compressed||page->compressed>XZ_OC_PAGE+4096||!page->bytes||page->bytes>XZ_OC_PAGE||page->bytes%4||
           (i+1<f->page_count&&page->bytes!=XZ_OC_PAGE)||page->offset+page->compressed>(uint64_t)file_bytes){free(table);return -1;}
        total+=page->bytes;previous=page->offset+page->compressed;
    }free(table);return total==f->bytes&&previous==(uint64_t)file_bytes?0:-1;
}
static int page_read(struct xz_oc_file *f,uint32_t index){
    if(index>=f->page_count)return -1;
    unsigned slot=0;
    for(unsigned i=0;i<XZ_OC_PAGE_SLOTS;i++){
        if(index==f->cached_page[i]){f->age[i]=++f->clock;return (int)i;}
        if(f->age[i]<f->age[slot])slot=i;
    }
    f->cached_page[slot]=UINT32_MAX;
    unsigned char *raw=f->raw+(size_t)slot*XZ_OC_PAGE;
    struct xz_oc_page *p=f->pages+index;unsigned char sum[32];
    if(p->offset>LONG_MAX||fseek(f->file,(long)p->offset,SEEK_SET)||fread(f->compressed,1,p->compressed,f->file)!=p->compressed)return -1;
    size_t n=tinfl_decompress_mem_to_mem(raw,XZ_OC_PAGE,f->compressed,p->compressed,TINFL_FLAG_PARSE_ZLIB_HEADER);
    if(n!=p->bytes)return -1;hash(raw,n,sum);if(memcmp(sum,p->sha,32))return -1;
    f->cached_page[slot]=index;f->age[slot]=++f->clock;return (int)slot;
}
int xz_oc_read(struct xz_oc_file *f,int64_t first,size_t count,int16_t *out){
    if(!f||!out||count>96000*4)return -1;
    for(size_t done=0;done<count;){
        int64_t at=first+(int64_t)done;
        if(at<0||((uint64_t)at)>=f->bytes/4){out[done*2]=out[done*2+1]=0;done++;continue;}
        uint64_t byte=(uint64_t)at*4;uint32_t page=(uint32_t)(byte/XZ_OC_PAGE);size_t offset=(size_t)(byte%XZ_OC_PAGE);
        int slot=page_read(f,page);if(slot<0)return -1;
        size_t n=(f->pages[page].bytes-offset)/4;if(n>count-done)n=count-done;
        for(size_t i=0;i<n*2;i++){unsigned char *p=f->raw+(size_t)slot*XZ_OC_PAGE+offset+i*2;out[done*2+i]=(int16_t)((unsigned)p[0]|(unsigned)p[1]<<8);}done+=n;
    }return 0;
}
static void close_file(struct xz_oc_file *f){if(f->file)fclose(f->file);free(f->pages);free(f->raw);free(f->compressed);memset(f,0,sizeof(*f));}
void xz_oc_close(struct xz_oc_assets *a){if(!a)return;for(unsigned i=0;i<3;i++){close_file(a->role+i);close_file(a->combination+i);free(a->wave[i]);}close_file(&a->reference);memset(a,0,sizeof(*a));}
struct xz_oc_file *xz_oc_mix_file(struct xz_oc_assets *a,unsigned mask){
    switch(mask){case 1:return a->role;case 2:return a->role+1;case 4:return a->role+2;
    case 3:return a->combination;case 5:return a->combination+1;case 6:return a->combination+2;case 7:return &a->reference;default:return NULL;}
}
float xz_oc_mix_gain(const struct xz_oc_assets *a,unsigned mask){
    switch(mask){case 1:return a->gain[0];case 2:return a->gain[1];case 4:return a->gain[2];
    case 3:return a->combination_gain[0];case 5:return a->combination_gain[1];case 6:return a->combination_gain[2];default:return 1;}
}
static void load_wave(const char *path,unsigned char **wave,uint32_t *count){
    FILE *f=fopen(path,"rb");if(!f)return;unsigned char h[24];
    if(fread(h,1,12,f)!=12||memcmp(h,"PMAI",4)||be32(h+4)<12||be32(h+8)>4*1024*1024){fclose(f);return;}
    uint32_t total=be32(h+8),offset=be32(h+4);
    while(offset+12<=total){
        if(fseek(f,(long)offset,SEEK_SET)||fread(h,1,12,f)!=12)break;
        uint32_t header=be32(h+4),length=be32(h+8);if(header<12||length<header||length>total-offset)break;
        if(!memcmp(h,"PWV3",4)&&header==24&&fread(h+12,1,12,f)==12&&be32(h+12)==1){
            uint32_t n=be32(h+16);if(n&&n<=length-header&&n<=2000000){
                unsigned char *data=malloc(n);if(data&&fread(data,1,n,f)==n){for(uint32_t i=0;i<n;i++)data[i]&=31;*wave=data;*count=n;}else free(data);
            }break;
        }offset+=length;
    }fclose(f);
}
int xz_oc_open(const char *source,struct xz_oc_assets *a,char *error,size_t capacity){
    struct document index={0},manifest={0};char path[4096],root[4096],bundle[32]="",value[4096],expected[65];int rc=-1;
    memset(a,0,sizeof(*a));snprintf(error,capacity,"Overcue cache is missing or invalid");
    if(strlen(source)>=sizeof(root))goto done;strcpy(root,source);for(char *p=root;*p;p++)if(*p=='\\')*p='/';
    char *relative=strstr(root,"/Contents/");if(!relative)goto done;char track[4096];strcpy(track,relative);*relative=0;
    if(path_join(path,root,"CDJMODS/index.json")||document_open(path,&index))goto done;
    if(!equal(&index,field(&index,0,"schema"),"overcue-index/1"))goto done;
    for(int t=1;t<index.count;t++)if(index.token[t].type==JSMN_OBJECT){
        int p=field(&index,t,"file_path");if(p<0||string(&index,p,value,sizeof(value))||strcmp(value,track))continue;
        if(string(&index,field(&index,t,"bundle"),value,sizeof(value))||strlen(value)!=16)goto done;
        for(unsigned i=0;i<16;i++)if(hex((unsigned char)value[i])<0)goto done;
        if(bundle[0]&&strcmp(bundle,value))goto done;strcpy(bundle,value);
    }
    if(!bundle[0])goto done;
    char folder[4096];if(snprintf(folder,sizeof(folder),"%s/CDJMODS/stems/%s",root,bundle)>=(int)sizeof(folder))goto done;
    if(path_join(path,folder,"overcue-manifest.json")||document_open(path,&manifest))goto done;
    if(!equal(&manifest,field(&manifest,0,"schema"),"overcue-stems/4"))goto done;
    int runtime=field(&manifest,0,"runtime"),src=field(&manifest,0,"source"),roles=field(&manifest,0,"roles");
    double frames=number(&manifest,field(&manifest,runtime,"frames"));
    if(number(&manifest,field(&manifest,runtime,"sample_rate"))!=96000||number(&manifest,field(&manifest,runtime,"channels"))!=2||
       !equal(&manifest,field(&manifest,runtime,"format"),"s16le")||frames<1||frames>500000000||frames!=floor(frames))goto done;
    a->frames=(uint64_t)frames;
    if(string(&manifest,field(&manifest,src,"sha256"),expected,sizeof(expected))||!file_hash_matches(source,expected)){
        snprintf(error,capacity,"Overcue stems do not match this source file");goto done;
    }
    const char *names[]={"drums","harmonics","vocal","full-mix","instrumental","vocals-drums","vocals-harmonics"};
    const unsigned masks[]={1,2,4,7,3,5,6};
    for(unsigned i=0;i<7;i++){
        int role=field(&manifest,roles,names[i]);double gain=number(&manifest,field(&manifest,role,"loudness_gain"));
        if(role<0||gain<=0||gain>8||number(&manifest,field(&manifest,role,"bytes"))!=frames*4||
           string(&manifest,field(&manifest,role,"page_table_sha256"),expected,sizeof(expected)))goto done;
        if(snprintf(path,sizeof(path),"%s/stems-sidecar-%s.s16le.pgz",folder,names[i])>=(int)sizeof(path)||
           open_pgz(path,expected,a->frames,xz_oc_mix_file(a,masks[i])))goto done;
        if(i>=4)a->combination_gain[i-4]=(float)gain;
        if(i<3){a->gain[i]=(float)gain;
            if(snprintf(path,sizeof(path),"%s/stems-%s-waveform.EXT",folder,names[i])<(int)sizeof(path))load_wave(path,&a->wave[i],&a->wave_count[i]);}
    }
    error[0]=0;rc=0;
done:
    document_close(&index);document_close(&manifest);if(rc)xz_oc_close(a);return rc;
}
