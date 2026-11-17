/* Temporary, read-only design viewer. Only accepts RGB565 scenes in /tmp.
 * The session wrapper pauses/restores the installed UI and owns the deadline.
 * This is a visual review tool, not the finished menu implementation. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __mips__
#include <sys/cachectl.h>
#endif
static volatile sig_atomic_t done;
static void stop(int signal_number){(void)signal_number;done=1;}
static int scene(const char *path,unsigned char *fb,size_t offset){
    if(strncmp(path,"/tmp/pocketos-design-",21))return 1;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(fd<0)return 1;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=480*800*2){close(fd);return 1;}
    size_t at=0;
    while(at<480*800*2){
        ssize_t n=read(fd,fb+offset+at,480*800*2-at);
        if(n<=0){close(fd);return 1;}at+=(size_t)n;
    }
    close(fd);
#ifdef __mips__
    if(cacheflush(fb+offset,480*800*2,DCACHE))return 1;
#endif
    return 0;
}
int main(int argc,char **argv){
    if(argc<3||strcmp(argv[1],"--ram-preview"))return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
    int fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);
    struct fb_var_screeninfo v;struct fb_fix_screeninfo f;
    if(fd<0)return 1;
    if(ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f)||
       v.xres!=480||v.yres!=800||v.bits_per_pixel!=16||f.line_length!=960||
       v.red.offset!=11||v.red.length!=5||v.green.offset!=5||v.green.length!=6||
       v.blue.offset!=0||v.blue.length!=5||v.xoffset||
       (size_t)(v.yoffset+800)*960>f.smem_len){close(fd);return 1;}
    unsigned char *fb=mmap(NULL,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(fb==MAP_FAILED){close(fd);return 1;}
    int input=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    char name[64]={0};struct input_absinfo x={0};
    if(input<0||ioctl(input,EVIOCGNAME(sizeof(name)),name)<0||strcmp(name,"hyn_ts")||
       ioctl(input,EVIOCGABS(ABS_MT_POSITION_X),&x)||x.maximum<=x.minimum||ioctl(input,EVIOCGRAB,1)){
        if(input>=0)close(input);
        munmap(fb,f.smem_len);close(fd);return 1;
    }
    int page=0,count=argc-2,down=0,rx=x.minimum,start=-1,pending=0,rc=0;
    size_t offset=(size_t)v.yoffset*960;
    if(scene(argv[2],fb,offset)){rc=1;goto finish;}
    printf("scene=1/%d\n",count);fflush(stdout);
    while(!done){
        struct pollfd p={.fd=input,.events=POLLIN};
        int ready=poll(&p,1,250);
        if(ready<0){if(errno==EINTR)continue;rc=1;break;}
        if(!ready)continue;
        struct input_event e;
        while(read(input,&e,sizeof(e))==(ssize_t)sizeof(e)){
            if(e.type==EV_ABS&&e.code==ABS_MT_POSITION_X){rx=e.value;if(down&&start<0)start=rx;}
            if(e.type==EV_KEY&&e.code==BTN_TOUCH){if(e.value){down=1;start=-1;}else if(down){down=0;pending=1;}}
            if(e.type==EV_SYN&&e.code==SYN_REPORT&&pending){
                pending=0;
                int delta=start<0?0:rx-start,range=x.maximum-x.minimum;
                int previous=delta>range/8||(delta>=-range/8&&rx-x.minimum<range/3);
                page=(page+(previous?count-1:1))%count;
                if(scene(argv[2+page],fb,offset)){rc=1;done=1;break;}
                printf("scene=%d/%d\n",page+1,count);fflush(stdout);
            }
        }
    }
finish:
    ioctl(input,EVIOCGRAB,0);close(input);munmap(fb,f.smem_len);close(fd);return rc;
}
