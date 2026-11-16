/* Read-only NAND geometry and bad-block inventory. No erase/write operations. */
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS read-only NAND inspection v1");return 0;}
    if(argc!=2||strcmp(argv[1],"/dev/mtd2")){fputs("Only /dev/mtd2 read-only inspection is supported\n",stderr);return 2;}
    int fd=open(argv[1],O_RDONLY|O_CLOEXEC);struct stat st;struct mtd_info_user info;
    if(fd<0||fstat(fd,&st)||!S_ISCHR(st.st_mode)||major(st.st_rdev)!=90||minor(st.st_rdev)!=4||ioctl(fd,MEMGETINFO,&info)){
        perror("MTD identity/geometry");return 1;
    }
    if(info.type!=MTD_NANDFLASH||info.size!=47185920||info.erasesize!=131072||info.writesize!=2048){
        fputs("Unexpected R1 rootfs NAND geometry\n",stderr);close(fd);return 1;
    }
    unsigned bad=0,total=info.size/info.erasesize;
    for(unsigned i=0;i<total;i++){
        int64_t offset=(int64_t)i*info.erasesize;
        int rc=ioctl(fd,MEMGETBADBLOCK,&offset);
        if(rc<0){perror("MEMGETBADBLOCK");close(fd);return 1;}
        if(rc){printf("bad_block_offset=%llu\n",(unsigned long long)offset);bad++;}
    }
    printf("device=/dev/mtd2\nread_only=1\nraw_bytes=%u\nerase_bytes=%u\npage_bytes=%u\nblocks=%u\nbad_blocks=%u\ngood_bytes=%llu\n",
           info.size,info.erasesize,info.writesize,total,bad,(unsigned long long)(total-bad)*info.erasesize);
    close(fd);return 0;
}
