/* Bounded, descriptor-relative microSD file operations. No shell commands. */
#ifndef POCKETOS_FILES_H
#define POCKETOS_FILES_H
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define FM_CAP 512
#define FM_PATH 768
#define FM_COPY_MAX (1024ULL*1024*1024) /* safe with the existing 32-bit target off_t */
struct fm_item {char name[256];int dir;unsigned long long bytes;};
static struct fm_item fm_items[FM_CAP];
static char fm_dir[FM_PATH],fm_source_dir[FM_PATH],fm_source_name[256];
static struct stat fm_source_stat;
static int fm_count,fm_index,fm_top,fm_clip,fm_truncated;
static int fm_root=-1;
static dev_t fm_device;
static int fm_progress_fd=-1;
static volatile sig_atomic_t fm_cancel_transfer;
static void fm_transfer_stop(int sig){(void)sig;fm_cancel_transfer=1;}
static void fm_progress(unsigned percent){if(fm_progress_fd>=0){ssize_t ignored=write(fm_progress_fd,&percent,sizeof(percent));(void)ignored;}}
static int fm_name_ok(const char *name){
    if(!name||!*name||*name=='.'||strlen(name)>255)return 0;
    for(const unsigned char *p=(const unsigned char*)name;*p;p++)if(*p<32||*p==127||*p=='/'||*p=='\\')return 0;
    return 1;
}
static void fm_close(void){
    if(fm_root>=0)close(fm_root);
    fm_root=-1;fm_dir[0]=0;fm_count=fm_top=fm_index=fm_clip=0;
}
static int fm_attach(const char *path){
    fm_close();
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(fd<0)return 0;
    if(fstat(fd,&st)!=0||!S_ISDIR(st.st_mode)){close(fd);return 0;}
    fm_root=fd;fm_device=st.st_dev;return 1;
}
static int fm_open_dir(const char *path){
    if(fm_root<0||strlen(path)>=FM_PATH||*path=='/')return -1;
    int fd=openat(fm_root,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)return -1;
    const char *p=path;
    while(*p){
        char name[256];size_t len=strcspn(p,"/");
        if(!len||len>=sizeof(name)){close(fd);return -1;}
        memcpy(name,p,len);name[len]=0;
        if(!fm_name_ok(name)){close(fd);return -1;}
        int next=openat(fd,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        close(fd);if(next<0)return -1;
        struct stat st;
        if(fstat(next,&st)!=0||st.st_dev!=fm_device){close(next);return -1;}
        fd=next;p+=len;if(*p=='/'){p++;if(!*p){close(fd);return -1;}}
    }
    return fd;
}
static int fm_user_dir(const char *path){
    const char *const allowed[]={"Music","Books","Documents","Downloads","Pictures","Videos"};
    for(size_t i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++){
        size_t n=strlen(allowed[i]);
        if(!strcmp(path,allowed[i])||(!strncmp(path,allowed[i],n)&&path[n]=='/'))return 1;
    }
    return 0;
}
/* Root-level loose media/documents may be moved into normal user folders.
 * Recovery images, update manifests, scripts and system directories remain protected. */
static int fm_user_file(const char *path,const char *name){
    if(!fm_name_ok(name))return 0;
    if(fm_user_dir(path))return 1;
    if(*path)return 0;
    const char *ext=strrchr(name,'.');
    const char *const allowed[]={".txt",".epub",".pdf",".md",".rtf",".doc",".docx",".csv",".html",".htm",".mobi",".azw3",".fb2",".mp3",".flac",".wav",".ogg",".opus",".aac",".m4a",".alac",".aiff",".ape",".wma",".jpg",".jpeg",".png",".gif",".bmp",".webp",".mp4",".mkv",".avi",".mov"};
    if(!ext)return 0;
    for(size_t i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)if(!strcasecmp(ext,allowed[i]))return 1;
    return 0;
}
static int fm_new_name_ok(const char *name){
    if(!fm_name_ok(name)||strpbrk(name,"<>:\"|?*"))return 0;
    size_t n=strlen(name);
    return name[n-1]!='.'&&name[n-1]!=' ';
}
static int fm_mkdir(const char *name){
    if(!fm_new_name_ok(name)||(!fm_user_dir(fm_dir)&&(*fm_dir||!fm_user_dir(name))))return 0;
    int fd=fm_open_dir(fm_dir);if(fd<0)return 0;
    int ok=mkdirat(fd,name,0700)==0;
    close(fd);if(ok)sync();return ok;
}
static int fm_cmp(const void *a,const void *b){
    const struct fm_item *x=a,*y=b;
    if(x->dir!=y->dir)return y->dir-x->dir;
    return strcasecmp(x->name,y->name);
}
static int fm_scan(void){
    fm_count=fm_top=fm_index=fm_truncated=0;
    int fd=fm_open_dir(fm_dir);if(fd<0)return 0;
    DIR *d=fdopendir(fd);if(!d){close(fd);return 0;}
    struct dirent *e;unsigned budget=4096;
    while(budget&&(e=readdir(d))){
        budget--;
        if(!fm_name_ok(e->d_name))continue;
        struct stat st;
        if(fstatat(fd,e->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0||st.st_dev!=fm_device)continue;
        if(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode))continue;
        if(fm_count>=FM_CAP){fm_truncated=1;break;}
        struct fm_item *i=&fm_items[fm_count++];
        strcpy(i->name,e->d_name);i->dir=S_ISDIR(st.st_mode);i->bytes=st.st_size>=0?(unsigned long long)st.st_size:0;
    }
    if(!budget)fm_truncated=1;
    closedir(d);qsort(fm_items,(size_t)fm_count,sizeof(fm_items[0]),fm_cmp);return 1;
}
static int fm_enter(const char *name){
    if(!fm_name_ok(name))return 0;
    char path[FM_PATH];
    int n=snprintf(path,sizeof(path),"%s%s%s",fm_dir,*fm_dir?"/":"",name);
    if(n<0||(size_t)n>=sizeof(path))return 0;
    int fd=fm_open_dir(path);if(fd<0)return 0;close(fd);
    char old[FM_PATH];strcpy(old,fm_dir);
    strcpy(fm_dir,path);if(fm_scan())return 1;
    strcpy(fm_dir,old);(void)fm_scan();return 0;
}
static int fm_up(void){
    if(!*fm_dir)return 0;
    char old[FM_PATH],child[256];strcpy(old,fm_dir);
    char *p=strrchr(fm_dir,'/');strcpy(child,p?p+1:fm_dir);
    if(p)*p=0;else fm_dir[0]=0;
    if(!fm_scan()){strcpy(fm_dir,old);(void)fm_scan();return 0;}
    for(int i=0;i<fm_count;i++)if(!strcmp(fm_items[i].name,child)){fm_index=i;fm_top=i>=5?i-4:0;break;}
    return 1;
}
static void fm_select(int step){
    if(!fm_count)return;
    int next=fm_index+step;
    fm_index=next<0?0:next>=fm_count?fm_count-1:next;
    if(fm_index<fm_top)fm_top=fm_index;
    if(fm_index>=fm_top+5)fm_top=fm_index-4;
}
static int fm_identity(const struct stat *a,const struct stat *b){
    return S_ISREG(b->st_mode)&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
        a->st_size==b->st_size&&a->st_mtime==b->st_mtime;
}
static int fm_capture(const char *name,int mode){
    if(!fm_user_file(fm_dir,name)||mode<1||mode>2)return 0;
    int fd=fm_open_dir(fm_dir);if(fd<0)return 0;
    struct stat st;
    int ok=fstatat(fd,name,&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode)&&st.st_dev==fm_device;
    close(fd);if(!ok)return 0;
    strcpy(fm_source_dir,fm_dir);strcpy(fm_source_name,name);fm_source_stat=st;fm_clip=mode;
    return 1;
}
static int fm_match(int fd,const char *name,const struct stat *expected){
    struct stat now;
    return fstatat(fd,name,&now,AT_SYMLINK_NOFOLLOW)==0&&fm_identity(expected,&now);
}
/* No overwrite: destination is created with O_EXCL. A move is copy+verify+delete.
 * Power loss can leave a partial destination; never delete before successful fsync. */
static int fm_transfer(const char *destination,int remove_source){
    if(!fm_clip||!fm_user_file(fm_dir,destination)||!fm_user_file(fm_source_dir,fm_source_name)||!fm_new_name_ok(destination))return 0;
    if(!strcmp(fm_dir,fm_source_dir)&&!strcmp(destination,fm_source_name))return 0;
    int a=fm_open_dir(fm_source_dir),b=fm_open_dir(fm_dir);
    if(a<0||b<0){if(a>=0)close(a);if(b>=0)close(b);return 0;}
    int src=-1,dst=-1,result=0,created=0;
    struct stat st;struct statvfs v;
    src=openat(a,fm_source_name,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(src<0||fstat(src,&st)!=0||!fm_identity(&fm_source_stat,&st)||st.st_size<0||
       (unsigned long long)st.st_size>FM_COPY_MAX||fstatvfs(b,&v)!=0||!v.f_frsize||
       (unsigned long long)v.f_bavail<(unsigned long long)(st.st_size+1048576ULL+v.f_frsize-1)/v.f_frsize)goto end;
    dst=openat(b,destination,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(dst<0)goto end;
    created=1;
    unsigned char buf[8192],check[8192];unsigned long long total=(unsigned long long)st.st_size,remaining=total;
    unsigned last=0;
    fm_progress(0);
    while(remaining){
        if(fm_cancel_transfer)goto end;
        size_t need=remaining>sizeof(buf)?sizeof(buf):(size_t)remaining;
        ssize_t n=read(src,buf,need);
        if(n<0&&errno==EINTR&&!fm_cancel_transfer)continue;
        if(n<=0||(size_t)n>need)goto end;
        size_t offset=0;
        while(offset<(size_t)n){
            ssize_t k=write(dst,buf+offset,(size_t)n-offset);
            if(k<0&&errno==EINTR&&!fm_cancel_transfer)continue;
            if(k<=0||(size_t)k>(size_t)n-offset)goto end;
            offset+=(size_t)k;
        }
        remaining-=(unsigned long long)n;
        unsigned pct=(unsigned)((total-remaining)*80/total);
        if(pct>last){last=pct;fm_progress(pct);}
    }
    if(fm_cancel_transfer||!fm_match(a,fm_source_name,&st)||fsync(dst)!=0)goto end;
    /* Read back moves before deleting the source. Both files stay open, and
       the source is checked again before any removal. */
    if(remove_source){
        if(lseek(src,0,SEEK_SET)<0||lseek(dst,0,SEEK_SET)<0)goto end;
        remaining=total;
        while(remaining){
            if(fm_cancel_transfer)goto end;
            size_t need=remaining>sizeof(buf)?sizeof(buf):(size_t)remaining;
            size_t offset=0;
            while(offset<need){
                if(offset>=sizeof(buf)||need>sizeof(buf)||offset>=need)goto end;
                size_t left=need-offset;
                ssize_t n=read(src,buf+offset,left);
                if(n<0&&errno==EINTR&&!fm_cancel_transfer)continue;
                if(n<=0||(size_t)n>left)goto end;
                offset+=(size_t)n;
            }
            offset=0;
            while(offset<need){
                if(offset>=sizeof(check)||need>sizeof(check)||offset>=need)goto end;
                size_t left=need-offset;
                ssize_t n=read(dst,check+offset,left);
                if(n<0&&errno==EINTR&&!fm_cancel_transfer)continue;
                if(n<=0||(size_t)n>left)goto end;
                offset+=(size_t)n;
            }
            if(memcmp(buf,check,need))goto end;
            remaining-=need;
            unsigned pct=80+(unsigned)((total-remaining)*19/total);
            if(pct>last){last=pct;fm_progress(pct);}
        }
    }
    if(fm_cancel_transfer||!fm_match(a,fm_source_name,&st))goto end;
    if(close(dst)!=0){dst=-1;goto end;}
    dst=-1;
    /* Keep the completed copy if removal fails. */
    created=0;result=1;
    if(remove_source){
        if(fm_cancel_transfer||fsync(b)!=0||!fm_match(a,fm_source_name,&st)||unlinkat(a,fm_source_name,0)!=0)result=2;
        else (void)fsync(a);
    }
    fm_progress(100);
end:
    if(dst>=0)close(dst);
    if(created)(void)unlinkat(b,destination,0);
    if(src>=0)close(src);
    close(a);close(b);
    if(result)fm_clip=0;
    return result;
}
static int fm_delete(void){
    if(!fm_clip||!fm_user_file(fm_source_dir,fm_source_name))return 0;
    int fd=fm_open_dir(fm_source_dir);if(fd<0)return 0;
    int ok=fm_match(fd,fm_source_name,&fm_source_stat)&&unlinkat(fd,fm_source_name,0)==0;
    close(fd);if(ok)fm_clip=0;return ok;
}
#endif
