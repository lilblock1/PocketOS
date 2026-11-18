/* Temporary test input for repeatable UI state/RAM checks on the observed R1.
 * Never included in firmware. No display, flash, mount or recovery operations. */
#define _DEFAULT_SOURCE 1
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "pocketos-hardware.h"
#include "pocketos-touch.h"
int main(int argc,char **argv){
    if(argc!=4||strcmp(argv[1],"--tap"))return 2;
    char *end;long x=strtol(argv[2],&end,10);if(*end||x<0||x>=480)return 2;
    long y=strtol(argv[3],&end,10);if(*end||y<0||y>=800)return 2;
    int fd=open("/dev/input/event1",O_RDWR|O_CLOEXEC);if(fd<0)return 1;
    char name[64]={0};struct input_absinfo ax={0},ay={0};
    if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0||strcmp(name,"hyn_ts")||
       ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&ax)||ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&ay)){close(fd);return 1;}
    (void)touch_calibrate("",name,&ax,&ay,480,800);
    if(ax.minimum||ay.minimum||ax.maximum!=480||ay.maximum!=800){close(fd);return 1;}
    struct input_event press[]={{.type=EV_ABS,.code=ABS_MT_POSITION_X,.value=x},
        {.type=EV_ABS,.code=ABS_MT_POSITION_Y,.value=y},
        {.type=EV_KEY,.code=BTN_TOUCH,.value=1},{.type=EV_SYN,.code=SYN_REPORT}};
    struct input_event release[]={{.type=EV_KEY,.code=BTN_TOUCH,.value=0},{.type=EV_SYN,.code=SYN_REPORT}};
    int ok=write(fd,press,sizeof(press))==(ssize_t)sizeof(press);usleep(60000);
    if(write(fd,release,sizeof(release))!=(ssize_t)sizeof(release))ok=0;
    close(fd);return ok?0:1;
}
