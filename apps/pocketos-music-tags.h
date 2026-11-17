/* Read-only bounded native music tags. Existing prepared sidecars override these.
 * No image decoding, subprocess, network access or large allocations. */
#ifndef POCKETOS_MUSIC_TAGS_H
#define POCKETOS_MUSIC_TAGS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define META_SCAN_MAX (8u*1024u*1024u)
static uint32_t meta_le32(const unsigned char *p){
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t meta_be32(const unsigned char *p){
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|(uint32_t)p[3];
}
static int meta_synch(const unsigned char *p,uint32_t *out){
    if((p[0]|p[1]|p[2]|p[3])&0x80)return 0;
    *out=(uint32_t)p[0]<<21|(uint32_t)p[1]<<14|(uint32_t)p[2]<<7|p[3];
    return 1;
}
static void meta_text(char dst[65],const unsigned char *src,size_t n){
    char value[65];size_t k=0;
    for(size_t i=0;i<n&&k<64&&src[i];i++){
        unsigned c=src[i];value[k++]=(char)(c>=32&&c<=126?c:' ');
    }
    while(k&&value[k-1]==' ')k--;
    size_t start=0;while(start<k&&value[start]==' ')start++;
    if(start==k)return;
    memcpy(dst,value+start,k-start);dst[k-start]=0;
}
static void meta_comments(FILE *f,uint32_t len,char title[65],char artist[65],char album[65]){
    unsigned char word[4],buf[256];
    if(len<8||fread(word,1,4,f)!=4)return;
    uint32_t vendor=meta_le32(word);len-=4;
    if(vendor>len-4||fseeko(f,(off_t)vendor,SEEK_CUR)||fread(word,1,4,f)!=4)return;
    len-=vendor+4;uint32_t count=meta_le32(word);
    for(uint32_t i=0;i<count&&i<256&&len>=4;i++){
        if(fread(word,1,4,f)!=4)return;
        uint32_t size=meta_le32(word);len-=4;if(size>len)return;
        size_t take=size<sizeof(buf)?size:sizeof(buf);
        if(fread(buf,1,take,f)!=take)return;
        if(take>=6&&!strncasecmp((const char*)buf,"TITLE=",6))meta_text(title,buf+6,take-6);
        else if(take>=7&&!strncasecmp((const char*)buf,"ARTIST=",7))meta_text(artist,buf+7,take-7);
        else if(take>=6&&!strncasecmp((const char*)buf,"ALBUM=",6))meta_text(album,buf+6,take-6);
        if(fseeko(f,(off_t)(size-take),SEEK_CUR))return;
        len-=size;
    }
}
static void meta_flac(FILE *f,char title[65],char artist[65],char album[65],uint32_t *duration){
    uint32_t budget=META_SCAN_MAX;unsigned char head[4],info[34];
    for(int i=0;i<64&&budget>=4;i++){
        if(fread(head,1,4,f)!=4)return;
        uint32_t len=(uint32_t)head[1]<<16|(uint32_t)head[2]<<8|head[3];budget-=4;
        if(len>budget)return;
        off_t start=ftello(f);if(start<0)return;
        unsigned type=head[0]&127;
        if(type==0&&len==34&&fread(info,1,34,f)==34){
            uint32_t rate=(uint32_t)info[10]<<12|(uint32_t)info[11]<<4|info[12]>>4;
            uint64_t samples=(uint64_t)(info[13]&15)<<32|meta_be32(info+14);
            if(rate&&samples){uint64_t sec=(samples+rate-1)/rate;if(sec<=604800)*duration=(uint32_t)sec;}
        }else if(type==4)meta_comments(f,len,title,artist,album);
        if(fseeko(f,start+(off_t)len,SEEK_SET))return;
        budget-=len;if(head[0]&128)return;
    }
}
static void meta_id3(FILE *f,const unsigned char head[10],char title[65],char artist[65],char album[65]){
    uint32_t remain=0;
    if((head[3]!=3&&head[3]!=4)||head[5]!=0||!meta_synch(head+6,&remain)||remain>META_SCAN_MAX)return;
    unsigned char frame[10],buf[256];
    for(int i=0;i<256&&remain>=10;i++){
        if(fread(frame,1,10,f)!=10||!frame[0])return;
        uint32_t size;
        if(head[3]==4){if(!meta_synch(frame+4,&size))return;}else size=meta_be32(frame+4);
        remain-=10;if(size>remain)return;
        char *dst=!memcmp(frame,"TIT2",4)?title:!memcmp(frame,"TPE1",4)?artist:!memcmp(frame,"TALB",4)?album:NULL;
        if(dst&&size>1&&!frame[8]&&!frame[9]){
            size_t take=size<sizeof(buf)?size:sizeof(buf);
            if(fread(buf,1,take,f)!=take)return;
            /* Latin-1 and UTF-8 ASCII subset. UTF-16 is intentionally deferred. */
            if(buf[0]==0||buf[0]==3)meta_text(dst,buf+1,take-1);
            if(fseeko(f,(off_t)(size-take),SEEK_CUR))return;
        }else if(fseeko(f,(off_t)size,SEEK_CUR))return;
        remain-=size;
    }
}
static void meta_read(FILE *f,char title[65],char artist[65],char album[65],uint32_t *duration){
    unsigned char head[10];
    if(fread(head,1,4,f)!=4)return;
    if(!memcmp(head,"fLaC",4)){meta_flac(f,title,artist,album,duration);return;}
    if(!memcmp(head,"ID3",3)&&fread(head+4,1,6,f)==6)meta_id3(f,head,title,artist,album);
}
#endif
