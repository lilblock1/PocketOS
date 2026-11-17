/* R1 boot-selection only, based on the recovered stock bootmode.sh.
 * Never writes kernel, rootfs, recovery or userdata. Do not enable untested.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mtd/mtd-user.h>

#define ERASE 131072u
#define PAGE 2048u
static unsigned char block[ERASE], page[PAGE];
static void make_flag(unsigned char *p,int recovery){
    memset(p,255,PAGE);memset(p,' ',256);
    const char *s=recovery?"ota:kernel2":"ota:kernel";memcpy(p,s,strlen(s));
}
static int primary_block(const unsigned char *b){
    make_flag(page,0);
    if(memcmp(b,page,PAGE))return 0;
    for(unsigned i=PAGE;i<ERASE;i++)if(b[i]!=255)return 0;
    return 1;
}
static int fail(const char *s){fprintf(stderr,"Recovery selection: %s\n",s);return 1;}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--self-test")){
        memset(block,255,sizeof(block));make_flag(block,0);
        if(!primary_block(block))return 1;
        block[ERASE-1]=0;if(primary_block(block))return 1;
        block[ERASE-1]=255;make_flag(block,1);
        if(primary_block(block)||memcmp(block,"ota:kernel2",11))return 1;
        for(unsigned i=256;i<PAGE;i++)if(block[i]!=255)return 1;
        return 0;
    }
    if(argc!=2||(strcmp(argv[1],"--check")&&strcmp(argv[1],"--arm")))return fail("invalid operation");
    int write_flag=!strcmp(argv[1],"--arm");
    if(geteuid()!=0)return fail("root required");
    struct stat st;
    if(lstat("/dev/mtd5",&st)||!S_ISCHR(st.st_mode))return fail("expected MTD character device");
    int fd=open("/dev/mtd5",(write_flag?O_RDWR:O_RDONLY)|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return fail("cannot open boot-selection partition");
    struct mtd_info_user info;
    __kernel_loff_t offset=0;
    if(ioctl(fd,MEMGETINFO,&info)||info.type!=MTD_NANDFLASH||info.size!=524288u||
       info.erasesize!=ERASE||info.writesize!=PAGE||ioctl(fd,MEMGETBADBLOCK,&offset)!=0){
        close(fd);return fail("unverified NAND geometry or bad first block");
    }
    if(pread(fd,block,sizeof(block),0)!=(ssize_t)sizeof(block)||!primary_block(block)){
        close(fd);return fail("not the known primary boot flag or unexpected first-block data");
    }
    if(!write_flag){close(fd);puts("Primary boot-selection block verified");return 0;}
    struct erase_info_user erase={0,ERASE};
    make_flag(page,1);
    if(ioctl(fd,MEMERASE,&erase)||pwrite(fd,page,sizeof(page),0)!=(ssize_t)sizeof(page)||
       fsync(fd)||pread(fd,block,sizeof(block),0)!=(ssize_t)sizeof(block)||memcmp(block,page,PAGE)){
        close(fd);return fail("boot-selection write/readback failed; DO NOT reboot");
    }
    for(unsigned i=PAGE;i<ERASE;i++)if(block[i]!=255){close(fd);return fail("unexpected readback; DO NOT reboot");}
    close(fd);puts("Recovery selection verified");return 0;
}
