/* R1 Smart Sleep: software-only, no kernel suspend writes. */
#ifndef POCKETOS_SUSPEND_H
#define POCKETOS_SUSPEND_H
#include <sys/utsname.h>
#include <linux/input.h>
#include <limits.h>
#include "pocketos-power.h"
static int smart_allow, smart_asleep;
struct suspend_key { long long pressed, ignore_until; int armed, dropped; };
static int suspend_key_event(struct suspend_key *k,const struct input_event *e,long long now){
    if(e->type==EV_SYN&&e->code==SYN_DROPPED){k->armed=0;k->dropped=1;return 0;}
    if(k->dropped){if(e->type==EV_SYN&&e->code==SYN_REPORT)k->dropped=0;return 0;}
    if(e->type!=EV_KEY||e->code!=KEY_POWER)return 0;
    if(now<0||now<k->ignore_until){k->armed=0;return 0;}
    long long stamp=e->time.tv_sec||e->time.tv_usec?(long long)e->time.tv_sec*1000+e->time.tv_usec/1000:now;
    if(e->value==1&&!k->armed){k->pressed=stamp;k->armed=1;return 0;}
    if(e->value!=0)return 0;
    int short_press=k->armed&&stamp-k->pressed>=30&&stamp-k->pressed<=800;
    k->armed=0;return short_press;
}
static int suspend_input_open(void){
    glob_t g={0};int result=-1;
    if(glob("/dev/input/event*",0,NULL,&g)!=0)return -1;
    for(size_t i=0;i<g.gl_pathc;i++){
        int fd=open(g.gl_pathv[i],O_RDONLY|O_CLOEXEC|O_NONBLOCK);
        if(fd<0)continue;
        char name[128]={0};unsigned char bits[(KEY_MAX+8)/8]={0};
        if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0||strcmp(name,"md-gpio-keys")||
           ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(bits)),bits)<0||!(bits[KEY_POWER/8]&(1u<<(KEY_POWER%8)))){close(fd);continue;}
        if(result>=0){close(result);close(fd);result=-1;break;}
        result=fd;
    }
    globfree(&g);return result;
}
static int smart_ready(void){
    struct utsname u;char buf[128];struct stat st;
    return smart_allow&&geteuid()==0&&!uname(&u)&&!strcmp(u.release,"4.4.94-sonix")&&
       !lstat("/etc/pocketos/smart-sleep-enable",&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&
       !(st.st_mode&022)&&power_attr_read("/etc/pocketos","smart-sleep-enable",buf,sizeof(buf))&&
       !strcmp(buf,"POCKETOS-R1-SMART-SLEEP-1");
}
/* Unknown is not connected. Read only the physically verified switch. */
static int smart_jack_value(const char *value){return !strcmp(value,"1")?1:!strcmp(value,"0")?0:-1;}
static int smart_jack(void){
    char buf[16];
    return power_sysfs("/sys/class/switch/headset")&&power_attr_read("/sys/class/switch/headset","state",buf,sizeof(buf))?smart_jack_value(buf):-1;
}
static int smart_keep_audio(int route,int jack){return route==0&&jack==1;}
static int smart_unlocked(void){
    struct stat st;
    if(lstat("/run/pocketos-update.lock",&st)==0||errno!=ENOENT)return 0;
    if(lstat("/mnt/sd_0/PocketOS-system-updates/.lock",&st)==0||errno!=ENOENT)return 0;
    if(lstat("/mnt/sd_0/PocketOS-updates/.lock",&st)==0||errno!=ENOENT)return 0;
    if(lstat("/run/pocketos-wifi/start.lock",&st)==0||errno!=ENOENT)return 0;
    return 1;
}
static int suspend_key_released(int fd){
    unsigned char bits[(KEY_MAX+8)/8]={0};
    return ioctl(fd,EVIOCGKEY(sizeof(bits)),bits)>=0&&!(bits[KEY_POWER/8]&(1u<<(KEY_POWER%8)));
}
static int suspend_key_test(void){
    struct suspend_key k={0};struct input_event e={.type=EV_KEY,.code=KEY_POWER,.value=1};
    if(suspend_key_event(&k,&e,100))return 1;
    e.value=2;if(suspend_key_event(&k,&e,200))return 2;
    e.value=0;if(!suspend_key_event(&k,&e,240)||suspend_key_event(&k,&e,250))return 3;
    e.value=1;(void)suspend_key_event(&k,&e,300);e.value=0;if(suspend_key_event(&k,&e,1200))return 4;
    k.ignore_until=3000;e.value=1;(void)suspend_key_event(&k,&e,2000);e.value=0;if(suspend_key_event(&k,&e,3100))return 5;
    e.value=1;(void)suspend_key_event(&k,&e,3200);e.type=EV_SYN;e.code=SYN_DROPPED;(void)suspend_key_event(&k,&e,3300);
    e.type=EV_KEY;e.code=KEY_POWER;e.value=0;if(suspend_key_event(&k,&e,3400))return 6;
    if(smart_ready()||suspend_key_released(-1))return 7; /* opt-in absent */
    if(smart_jack_value("0")!=0||smart_jack_value("1")!=1||smart_jack_value("2")!=-1||smart_jack_value("")!=-1)return 8;
    if(!smart_keep_audio(0,1)||smart_keep_audio(0,0)||smart_keep_audio(0,-1)||smart_keep_audio(1,1)||smart_keep_audio(2,1))return 9;
    return 0;
}
#endif
