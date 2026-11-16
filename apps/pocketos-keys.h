/* R1 volume keys arrive on separate evdev devices from its touchscreen. */
#ifndef POCKETOS_KEYS_H
#define POCKETOS_KEYS_H
#include <linux/input.h>
#include <fcntl.h>
#include <glob.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct volume_input { int fd, dropped; long long repeat_at; };
#define VOLUME_INPUTS 3
static int volume_input_open(struct volume_input inputs[VOLUME_INPUTS],const char *touch_path){
    glob_t paths={0};int count=0;
    if(glob("/dev/input/event*",GLOB_NOSORT,NULL,&paths)!=0)return 0;
    for(size_t i=0;i<paths.gl_pathc&&count<VOLUME_INPUTS;i++){
        if(!strcmp(paths.gl_pathv[i],touch_path))continue;
        int fd=open(paths.gl_pathv[i],O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if(fd<0)continue;
        char name[128]={0};
        if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0||
           (strcmp(name,"jz adc keyboard")&&strcmp(name,"earpods_adc"))){close(fd);continue;}
        inputs[count++]=(struct volume_input){.fd=fd};
        fprintf(stderr,"volume-input=%s name=%s\n",paths.gl_pathv[i],name);
    }
    globfree(&paths);return count;
}
static int volume_input_event(struct volume_input *input,const struct input_event *event,long long now){
    if(event->type==EV_SYN&&event->code==SYN_DROPPED){input->dropped=1;return 0;}
    if(input->dropped){if(event->type==EV_SYN&&event->code==SYN_REPORT)input->dropped=0;return 0;}
    if(event->type!=EV_KEY||(event->code!=KEY_VOLUMEUP&&event->code!=KEY_VOLUMEDOWN))return 0;
    if(event->value!=1&&event->value!=2)return 0;
    if(event->value==2&&now<input->repeat_at)return 0;
    input->repeat_at=now+120;
    return event->code==KEY_VOLUMEUP?1:-1;
}
#endif
