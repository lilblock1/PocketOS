/* Attach only the existing R1 userdata UBI. No raw writes, erase or formatting. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <dirent.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <mtd/ubi-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
static int number(const char *path){
    FILE *f=fopen(path,"r");int n=-1;char extra;
    if(!f)return -1;
    if(fscanf(f,"%d %c",&n,&extra)!=1)n=-1;
    fclose(f);return n;
}
static int attached(void){
    DIR *d=opendir("/sys/class/ubi");if(!d)return -1;
    struct dirent *entry;int found=-1;
    while((entry=readdir(d))){
        const char *s=entry->d_name;
        if(strncmp(s,"ubi",3)||!s[3]||strspn(s+3,"0123456789")!=strlen(s+3))continue;
        char path[320];snprintf(path,sizeof(path),"/sys/class/ubi/%s/mtd_num",s);
        if(number(path)==6){if(found>=0){found=-2;break;}found=atoi(s+3);}
    }
    closedir(d);return found;
}
int main(int argc,char **argv){
    if(argc!=2||(strcmp(argv[1],"--check")&&strcmp(argv[1],"--attach")))return 2;
    FILE *name=fopen("/sys/class/mtd/mtd6/name","r");char text[32]={0};
    if(!name)return 1;
    int valid=fgets(text,sizeof(text),name)!=NULL;fclose(name);
    if(!valid||strcmp(text,"userdata\n"))return 1;
    struct stat st;struct mtd_info_user info;char magic[4];
    int fd=open("/dev/mtd6",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    valid=fd>=0&&!fstat(fd,&st)&&S_ISCHR(st.st_mode)&&major(st.st_rdev)==90&&minor(st.st_rdev)==12&&
        !ioctl(fd,MEMGETINFO,&info)&&info.type==MTD_NANDFLASH&&info.size==47185920&&
        info.erasesize==131072&&info.writesize==2048&&pread(fd,magic,4,0)==4&&!memcmp(magic,"UBI#",4);
    if(fd>=0)close(fd);
    if(!valid){fputs("Existing userdata identity/UBI header differs\n",stderr);return 1;}
    if(!strcmp(argv[1],"--check")){puts("Existing userdata identity verified; no attachment or writes");return 0;}
    fd=open("/etc/pocketos/state-enable",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    memset(text,0,sizeof(text));
    valid=geteuid()==0&&fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&
        !(st.st_mode&022)&&st.st_size==20&&read(fd,text,sizeof(text))==20&&!strcmp(text,"POCKETOS-R1-STATE-1\n");
    if(fd>=0)close(fd);
    if(!valid)return 1;
    int ubi=attached();if(ubi==-2)return 1;
    if(ubi<0){
        fd=open("/dev/ubi_ctrl",O_RDWR|O_NOFOLLOW|O_CLOEXEC);
        struct ubi_attach_req request={.ubi_num=UBI_DEV_NUM_AUTO,.mtd_num=6};
        valid=fd>=0&&!fstat(fd,&st)&&S_ISCHR(st.st_mode)&&!ioctl(fd,UBI_IOCATT,&request);
        if(fd>=0)close(fd);
        if(!valid){perror("Existing userdata attach failed; refusing to format");return 1;}
        ubi=request.ubi_num;
    }
    if(ubi<0||ubi>31)return 1;
    printf("ubi%d\n",ubi);return 0;
}
