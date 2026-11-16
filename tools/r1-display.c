/* Temporary test support: query framebuffer/input and save/restore pixels.
 * Explicit unblank activates the cold-boot display; no mode reset or flash. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <time.h>
#ifdef __mips__
#include <sys/cachectl.h>
#endif
struct header { uint32_t magic; struct fb_var_screeninfo v; struct fb_fix_screeninfo f; };
static int geometry_equal(struct header *a,struct header *b){
    return a->magic==b->magic&&a->v.xres==b->v.xres&&a->v.yres==b->v.yres&&
        a->v.xres_virtual==b->v.xres_virtual&&a->v.yres_virtual==b->v.yres_virtual&&
        a->v.xoffset==b->v.xoffset&&a->v.bits_per_pixel==b->v.bits_per_pixel&&
        !memcmp(&a->v.red,&b->v.red,sizeof(a->v.red))&&!memcmp(&a->v.green,&b->v.green,sizeof(a->v.green))&&
        !memcmp(&a->v.blue,&b->v.blue,sizeof(a->v.blue))&&a->f.line_length==b->f.line_length&&a->f.smem_len==b->f.smem_len;
}
int main(int argc,char **argv){
    if(argc<2||argc>3)return 2;
    int save=!strcmp(argv[1],"save"),restore=!strcmp(argv[1],"restore"),inspect=!strcmp(argv[1],"inspect"),unblank=!strcmp(argv[1],"unblank");
    if((!save&&!restore&&!inspect&&!unblank)||((save||restore)&&argc!=3)||(unblank&&argc!=2))return 2;
    struct header h={.magic=0x504f5346};
    int fb=open("/dev/fb0",((restore||unblank)?O_RDWR:O_RDONLY)|O_CLOEXEC);if(fb<0){perror("framebuffer");return 1;}
    if(ioctl(fb,FBIOGET_VSCREENINFO,&h.v)<0||ioctl(fb,FBIOGET_FSCREENINFO,&h.f)<0){perror("framebuffer query");close(fb);return 1;}
    if(!h.f.smem_len||h.f.smem_len>4*1024*1024||!h.v.xres||!h.v.yres){close(fb);return 1;}
    if(unblank){
        if(h.v.xres!=480||h.v.yres!=800||h.v.bits_per_pixel!=16||h.f.line_length!=960){close(fb);return 1;}
        int rc=ioctl(fb,FBIOBLANK,FB_BLANK_UNBLANK);
        if(rc<0)perror("framebuffer unblank");else puts("framebuffer_unblank=ok");
        close(fb);return rc<0?1:0;
    }
    if(inspect){
        printf("visible=%ux%u\nvirtual=%ux%u\noffset=%u,%u\nbpp=%u\nstride=%u\nframebuffer_bytes=%u\nred=%u:%u\ngreen=%u:%u\nblue=%u:%u\nvisual=%u\n",
            h.v.xres,h.v.yres,h.v.xres_virtual,h.v.yres_virtual,h.v.xoffset,h.v.yoffset,h.v.bits_per_pixel,
            h.f.line_length,h.f.smem_len,h.v.red.offset,h.v.red.length,h.v.green.offset,h.v.green.length,h.v.blue.offset,h.v.blue.length,h.f.visual);
        close(fb);
        if(argc==3){
            if(strncmp(argv[2],"/dev/input/event",16))return 2;
            int fd=open(argv[2],O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)return 1;
            char name[128]={0};struct input_absinfo x={0},y={0};
            if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0){close(fd);return 1;}
            int mt=ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&x)==0&&ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&y)==0;
            if(!mt&&(ioctl(fd,EVIOCGABS(ABS_X),&x)<0||ioctl(fd,EVIOCGABS(ABS_Y),&y)<0)){close(fd);return 1;}
            printf("input_name=%s\ntouch_protocol=%s\nx_range=%d,%d\ny_range=%d,%d\n",name,mt?"MT":"ABS",x.minimum,x.maximum,y.minimum,y.maximum);
            close(fd);
        }
        return 0;
    }
    if(strncmp(argv[2],"/tmp/",5)){close(fb);return 2;}
    int fd=open(argv[2],(save?O_WRONLY|O_CREAT|O_EXCL:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0){perror("snapshot");close(fb);return 1;}
    struct statfs fs;struct stat st;
    if(fstatfs(fd,&fs)||fstat(fd,&st)||!S_ISREG(st.st_mode)||(unsigned long)fs.f_type!=0x01021994UL){close(fd);close(fb);return 1;}
    unsigned char *mem=mmap(NULL,h.f.smem_len,PROT_READ|(restore?PROT_WRITE:0),MAP_SHARED,fb,0);
    if(mem==MAP_FAILED){perror("framebuffer map");close(fd);close(fb);return 1;}
    int ok=1;
    if(save){
        if(write(fd,&h,sizeof(h))!=(ssize_t)sizeof(h))ok=0;
        for(size_t at=0;ok&&at<h.f.smem_len;){ssize_t n=write(fd,mem+at,h.f.smem_len-at);if(n<=0)ok=0;else at+=(size_t)n;}
    }else{
        struct header old;unsigned char *pixels=malloc(h.f.smem_len);
        if(!pixels||st.st_size!=(off_t)(sizeof(old)+h.f.smem_len)||read(fd,&old,sizeof(old))!=(ssize_t)sizeof(old)||!geometry_equal(&old,&h))ok=0;
        for(size_t at=0;ok&&at<h.f.smem_len;){ssize_t n=read(fd,pixels+at,h.f.smem_len-at);if(n<=0)ok=0;else at+=(size_t)n;}
        if(ok){
            memcpy(mem,pixels,h.f.smem_len);
#ifdef __mips__
            if(cacheflush(mem,(int)h.f.smem_len,DCACHE)<0)ok=0;
#endif
            if(old.v.yoffset!=h.v.yoffset){
                int flipped=0;
                for(int i=0;i<20;i++){
                    if(ioctl(fb,FBIOPAN_DISPLAY,&old.v)==0){flipped=1;break;}
                    if(errno!=EBUSY&&errno!=EAGAIN)break;
                    struct timespec pause={0,20000000};nanosleep(&pause,NULL);
                }
                if(!flipped)ok=0;
            }
        }
        free(pixels);
    }
    munmap(mem,h.f.smem_len);close(fb);if(close(fd))ok=0;return ok?0:1;
}
