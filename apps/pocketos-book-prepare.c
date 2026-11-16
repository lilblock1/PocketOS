/* SPDX-License-Identifier: GPL-3.0-only
 * Native unencrypted EPUB text preparation, using Sonix's bounded parser.
 * A separate process writes an adjacent text cache; originals stay unchanged.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE 1
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "epub_arena.h"
#include "epub_zip.h"
#include "epub_xml.h"

#define ITEM_CAP 4096
#define TEXT_CAP 67108864u
struct item { char *id,*href; };
static struct item items[ITEM_CAP];
static unsigned spine[ITEM_CAP], item_count, spine_count;
static volatile sig_atomic_t interrupted;
static FILE *output;
static unsigned output_bytes;
static int last='\n',pending_space,failed;
static unsigned prose_since_break;
static int writing_heading;
static char publication_title[256],publication_author[256];
static void stop(int n){(void)n;interrupted=1;}
static void put(int c){
    if(failed)return;
    if(output_bytes>=TEXT_CAP||fputc(c,output)==EOF){failed=1;return;}
    output_bytes++;last=c;
}
static void line(void){pending_space=0;if(output_bytes&&last!='\n')put('\n');}
static void text(const char *s,size_t len){
    while(len&&!failed){
        size_t take=len>1024?1024:len;
        /* Keep a reference at a chunk boundary together for the unescaper. */
        if(take<len){
            size_t start=take;
            while(start&&take-start<12&&s[start-1]!='&'&&s[start-1]!=';')start--;
            if(start&&s[start-1]=='&')take=start-1;
            if(!take)take=len>1024?1024:len;
        }
        char plain[4096];size_t n=epub_xml_unescape(plain,sizeof(plain),s,take);
        for(size_t i=0;i<n;i++){
            unsigned char c=(unsigned char)plain[i];
            if(isspace(c)){pending_space=1;continue;}
            if(pending_space&&output_bytes&&last!='\n')put(' ');
            pending_space=0;put(c);
            if(!writing_heading)prose_since_break++;
        }
        s+=take;len-=take;
    }
}
static int attr(epub_xml_t *x,const char *name,char out[1024]){
    const char *s;size_t len;
    if(!epub_xml_attr(x,name,&s,&len)||!len||len>=1024)return 0;
    size_t got=epub_xml_unescape(out,1024,s,len);
    return got>0&&got<1023;
}
static int hex(unsigned char c){
    if(c>='0'&&c<='9')return c-'0';
    c=(unsigned char)tolower(c);return c>='a'&&c<='f'?c-'a'+10:-1;
}
static int resolve(const char *opf,const char *href,char out[1024]){
    char joined[2048],decoded[1024];size_t used=0;
    for(size_t i=0;href[i]&&href[i]!='#';i++){
        unsigned char c=(unsigned char)href[i];
        if(c=='%'){
            if(!href[i+1]||!href[i+2]||hex((unsigned char)href[i+1])<0||hex((unsigned char)href[i+2])<0)return 0;
            c=(unsigned char)(hex((unsigned char)href[i+1])*16+hex((unsigned char)href[i+2]));i+=2;
        }
        if(!c||c<' '||c=='\\'||c==':'||used>=sizeof(decoded)-1)return 0;
        decoded[used++]=(char)c;
    }
    decoded[used]=0;if(!used||decoded[0]=='/')return 0;
    const char *slash=strrchr(opf,'/');size_t base=slash?(size_t)(slash-opf)+1:0;
    if(base+used>=sizeof(joined))return 0;
    memcpy(joined,opf,base);memcpy(joined+base,decoded,used+1);
    used=0;char *save=NULL;
    for(char *part=strtok_r(joined,"/",&save);part;part=strtok_r(NULL,"/",&save)){
        if(!strcmp(part,"."))continue;
        if(!strcmp(part,"..")){
            if(!used)return 0;
            while(used&&out[used-1]!='/')used--;
            if(used)used--;
            continue;
        }
        size_t n=strlen(part);if(used+n+1>=1024)return 0;
        if(used)out[used++]='/';
        memcpy(out+used,part,n);used+=n;
    }
    out[used]=0;return used>0;
}
static int hidden(epub_xml_t *x){return epub_xml_is(x,"head")||epub_xml_is(x,"script")||epub_xml_is(x,"style")||epub_xml_is(x,"svg");}
static int block(epub_xml_t *x){
    const char *names[]={"p","div","br","h1","h2","h3","h4","h5","h6","li","section","article","blockquote","tr","hr"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)if(epub_xml_is(x,names[i]))return 1;
    return 0;
}
static int heading(epub_xml_t *x){return epub_xml_is(x,"h1")||epub_xml_is(x,"h2")||epub_xml_is(x,"h3")||epub_xml_is(x,"h4")||epub_xml_is(x,"h5")||epub_xml_is(x,"h6");}
static void chapter(const char *doc){
    epub_xml_t x;epub_xml_init(&x,doc);unsigned hide=0;
    while(!interrupted&&!failed&&epub_xml_next(&x)){
        if(x.event==EPUB_XML_TEXT){if(!hide)text(x.text,x.text_len);continue;}
        if(hidden(&x)){
            if(x.event==EPUB_XML_OPEN&&!x.self_closing)hide++;
            else if(x.event==EPUB_XML_CLOSE&&hide)hide--;
        }else if(!hide&&heading(&x)){
            line();
            if(x.event==EPUB_XML_OPEN){
                if(prose_since_break>200){put('\f');prose_since_break=0;}
                put('#');put(' ');writing_heading=1;
            }else writing_heading=0;
        }else if(!hide&&block(&x))line();
    }
    line();
}
static int publication(epub_zip_t *zip,epub_arena_t *meta){
    for(uint32_t i=0;i<zip->count;i++){
        const char *n=epub_zip_name(zip,i);
        if(!strcasecmp(n,"META-INF/encryption.xml")||!strcasecmp(n,"META-INF/license.lcpl")||!strcasecmp(n,"META-INF/rights.xml")){
            fputs("Protected publication requires its authorized reader\n",stderr);return 0;
        }
    }
    char *doc=epub_zip_extract(zip,epub_zip_find(zip,"META-INF/container.xml"),meta,65536,NULL);
    if(!doc)return 0;
    char opf[1024]={0},value[1024];epub_xml_t x;epub_xml_init(&x,doc);
    while(epub_xml_next(&x))if(x.event==EPUB_XML_OPEN&&epub_xml_is(&x,"rootfile")&&attr(&x,"full-path",value)){
        if(!resolve("",value,opf))return 0;
        break;
    }
    if(!opf[0])return 0;
    doc=epub_zip_extract(zip,epub_zip_find(zip,opf),meta,2097152,NULL);if(!doc)return 0;
    epub_xml_init(&x,doc);int in_spine=0,metadata_field=0;
    while(epub_xml_next(&x)){
        if(epub_xml_is(&x,"title"))metadata_field=x.event==EPUB_XML_OPEN?1:0;
        if(epub_xml_is(&x,"creator"))metadata_field=x.event==EPUB_XML_OPEN?2:0;
        if(x.event==EPUB_XML_TEXT&&metadata_field){
            char *target=metadata_field==1?publication_title:publication_author;
            if(!target[0]){
                epub_xml_unescape(target,256,x.text,x.text_len);
                for(char *p=target;*p;p++)if((unsigned char)*p<' ')*p=' ';
            }
            continue;
        }
        if(epub_xml_is(&x,"spine")){in_spine=x.event==EPUB_XML_OPEN;continue;}
        if(x.event!=EPUB_XML_OPEN)continue;
        if(epub_xml_is(&x,"item")){
            char href[1024],member[1024];
            if(!attr(&x,"id",value)||!attr(&x,"href",href)||!resolve(opf,href,member)||item_count>=ITEM_CAP)return 0;
            items[item_count].id=arena_strdup(meta,value);items[item_count].href=arena_strdup(meta,member);
            if(!items[item_count].id||!items[item_count].href)return 0;
            item_count++;
        }else if(in_spine&&epub_xml_is(&x,"itemref")){
            char linear[1024];if(attr(&x,"linear",linear)&&!strcasecmp(linear,"no"))continue;
            if(!attr(&x,"idref",value)||spine_count>=ITEM_CAP)return 0;
            unsigned i=0;for(;i<item_count;i++)if(!strcmp(items[i].id,value))break;
            if(i==item_count)return 0;
            spine[spine_count++]=i;
        }
    }
    if(!spine_count)return 0;
    for(unsigned i=0;i<spine_count&&!interrupted&&!failed;i++){
        epub_arena_t part;arena_init(&part,"chapter",65536);
        doc=epub_zip_extract(zip,epub_zip_find(zip,items[spine[i]].href),&part,8388608,NULL);
        if(!doc){arena_destroy(&part);return 0;}
        if(output_bytes){
            line();
            if(prose_since_break>200){put('\f');prose_since_break=0;}else put('\n');
        }
        chapter(doc);arena_destroy(&part);
    }
    return !interrupted&&!failed&&output_bytes>0;
}
static int metadata_file(const char *input){
    char dest[1200],temp[1240];struct stat st;
    if(snprintf(dest,sizeof(dest),"%s.pocket.meta",input)>=(int)sizeof(dest))return 0;
    if(lstat(dest,&st)==0&&!S_ISREG(st.st_mode))return 0;
    snprintf(temp,sizeof(temp),"%s.tmp.XXXXXX",dest);
    int fd=mkstemp(temp);if(fd<0)return 0;
    FILE *f=fdopen(fd,"w");int ok=0;
    if(f){ok=fprintf(f,"POCKETOS-BOOK-1\n%s\n%s\n",publication_title[0]?publication_title:"Book",publication_author)>0;
        if(fflush(f)||fsync(fd))ok=0;
        if(fclose(f))ok=0;
    }else close(fd);
    if(ok&&rename(temp,dest)==0)return 1;
    unlink(temp);return 0;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS native EPUB preparation v1 (GPL-3.0)");return 0;}
    if(argc!=3||strcmp(argv[1],"--epub")||argv[2][0]!='/')return 2;
    char dest[1200],tmp[1240];
    if(snprintf(dest,sizeof(dest),"%s.pocket.txt",argv[2])>=(int)sizeof(dest)||
       snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",dest)>=(int)sizeof(tmp))return 2;
    struct stat st;if(lstat(argv[2],&st)||!S_ISREG(st.st_mode)||st.st_size>67108864)return 1;
    if(lstat(dest,&st)==0)return S_ISREG(st.st_mode)&&st.st_size>0?0:1;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGALRM,stop);alarm(120);
    epub_arena_t meta;arena_init(&meta,"book metadata",65536);epub_zip_t zip;
    if(!epub_zip_open(&zip,argv[2],&meta)){arena_destroy(&meta);fputs("Cannot read EPUB archive\n",stderr);return 1;}
    int fd=mkstemp(tmp);if(fd<0){epub_zip_close(&zip);arena_destroy(&meta);perror("Book cache");return 1;}
    output=fdopen(fd,"w");int ok=output&&publication(&zip,&meta);
    if(output){if(fflush(output)||fsync(fd))ok=0;if(fclose(output))ok=0;}else close(fd);
    epub_zip_close(&zip);arena_destroy(&meta);
    if(ok&&metadata_file(argv[2])&&rename(tmp,dest)==0){puts("Book text prepared on microSD");return 0;}
    unlink(tmp);fputs("EPUB text preparation failed; original unchanged\n",stderr);return 1;
}
