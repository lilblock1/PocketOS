/* PocketOS HiBy R1 backend for doomgeneric. GPL-2.0-or-later.
 * Uses the already configured RGB565 framebuffer; no display mode, flash,
 * recovery, mount, backlight or audio-register writes. No SDL/X dependency.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifdef __mips__
#include <sys/cachectl.h>
#endif
#include "doomgeneric.h"
#include "doomstat.h"
#include "pocketos-hardware.h"
#include "pocketos-touch.h"

enum { D_UP=0xad,D_DOWN=0xaf,D_LEFT=0xac,D_RIGHT=0xae,D_FIRE=0xa3,D_USE=0xa2,
       D_ENTER=13,D_ESCAPE=27,D_MAP=9,D_WEAPON='2' };
enum { UP=1,DOWN=2,LEFT=4,RIGHT=8,FIRE=16,USE=32,ENTER=64,MENU=128,MAP=256,WEAPON=512 };
static const unsigned char keys[]={D_UP,D_DOWN,D_LEFT,D_RIGHT,D_FIRE,D_USE,D_ENTER,D_ESCAPE,D_MAP,D_WEAPON};
static int fb_fd=-1,inputs[2]={-1,-1},headless,frame_limit,frames;
static unsigned char *fb;
static size_t fb_bytes,fb_offset;
static struct input_absinfo ax,ay;
static unsigned touch_mask,physical_mask,held;
static unsigned char weapon_key='2';
static uint16_t queue[64];static unsigned qread,qwrite;
static int rx,ry,have_x,have_y,down,dropped[2];
static volatile sig_atomic_t stopping;
static uint64_t epoch;
static const char *output;
#define COLOUR(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
static uint64_t millis(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void signal_stop(int number){(void)number;stopping=1;}
static void cleanup(void){
    for(int i=0;i<2;i++)if(inputs[i]>=0){ioctl(inputs[i],EVIOCGRAB,0);close(inputs[i]);inputs[i]=-1;}
    if(fb){munmap(fb,fb_bytes);fb=NULL;}if(fb_fd>=0){close(fb_fd);fb_fd=-1;}
}
static void fail(const char *message){perror(message);exit(1);}
static void point(int x,int y,uint16_t c){if(fb&&x>=0&&x<480&&y>=0&&y<800)((uint16_t *)(fb+fb_offset))[y*480+x]=c;}
static void box(int x,int y,int w,int h,uint16_t c){for(int j=0;j<h;j++)for(int i=0;i<w;i++)point(x+i,y+j,c);}
static const unsigned char letters[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static void label(int x,int y,const char *text,int scale){
    for(;*text;text++,x+=6*scale)if(*text>='A'&&*text<='Z'){
        for(int j=0;j<7;j++)for(int i=0;i<5;i++)if(letters[*text-'A'][j]&(1u<<(4-i)))box(x+i*scale,y+j*scale,scale,scale,COLOUR(228,237,249));
    }
}
static void button(int x,int y,int w,int h,const char *text,unsigned bit){
    box(x,y,w,h,held&bit?COLOUR(41,94,158):COLOUR(35,46,63));
    label(x+(w-(int)strlen(text)*12)/2,y+(h-14)/2,text,2);
}
static void topbar(void){
    button(5,370,112,48,"MENU",MENU);button(124,370,112,48,"ENTER",ENTER);
    button(243,370,112,48,"MAP",MAP);button(362,370,112,48,"EXIT",0);
}
static void controls(void){
    box(0,360,480,440,COLOUR(14,20,29));topbar();
    button(80,445,90,75,"UP",UP);button(80,620,90,75,"DOWN",DOWN);
    button(5,532,105,75,"LEFT",LEFT);button(140,532,105,75,"RIGHT",RIGHT);
    button(275,445,195,105,"FIRE",FIRE);button(275,568,195,105,"USE",USE);
    button(275,690,195,65,"WEAPON",WEAPON);
    label(45,775,"VOLUME UP FIRE   VOLUME DOWN USE",2);
}
static unsigned touch_keys(int x,int y){
    if(y>=370&&y<418){if(x<120)return MENU;if(x<240)return ENTER;if(x<360)return MAP;return 0;}
    if(y>=445&&y<695&&x<250){
        int dx=x-125,dy=y-570;
        if(abs(dx)<35&&abs(dy)<35)return 0;
        unsigned result=0;
        if(dy<-35)result|=UP;else if(dy>35)result|=DOWN;
        if(dx<-35)result|=LEFT;else if(dx>35)result|=RIGHT;
        return result;
    }
    if(x>=275&&x<475){if(y>=445&&y<550)return FIRE;if(y>=568&&y<673)return USE;if(y>=690&&y<755)return WEAPON;}
    return 0;
}
static void refresh_keys(void){
    unsigned next=touch_mask|physical_mask,change=next^held;
    for(unsigned i=0;i<sizeof(keys);i++)if(change&(1u<<i)){
        unsigned at=(qwrite+1)%64;if(at==qread){stopping=1;return;}
        unsigned char code=keys[i];
        if((1u<<i)==WEAPON){
            if(next&WEAPON){
                player_t *player=&players[consoleplayer>=0&&consoleplayer<MAXPLAYERS?consoleplayer:0];
                for(int step=1;step<=NUMWEAPONS;step++){
                    int candidate=(player->readyweapon+step)%NUMWEAPONS;
                    if(player->weaponowned[candidate]){weapon_key=(unsigned char)('1'+(candidate==wp_chainsaw?0:candidate==wp_supershotgun?2:candidate));break;}
                }
            }
            code=weapon_key;
        }
        queue[qwrite]=(uint16_t)(((next&(1u<<i))?256:0)|code);qwrite=at;
    }
    if(change){held=next;if(!headless)controls();}
}
static void poll_input(void){
    for(int i=0;i<2;i++){
        if(inputs[i]<0)continue;
        struct input_event events[16];ssize_t bytes;
        while((bytes=read(inputs[i],events,sizeof(events)))>0){
            if((size_t)bytes%sizeof(events[0])){stopping=1;return;}
            for(size_t n=0;n<(size_t)bytes/sizeof(events[0]);n++){
                struct input_event e=events[n];
                if(e.type==EV_SYN&&e.code==SYN_DROPPED){dropped[i]=1;if(!i){down=touch_mask=have_x=have_y=0;}else physical_mask=0;refresh_keys();continue;}
                if(dropped[i]){if(e.type==EV_SYN&&e.code==SYN_REPORT)dropped[i]=0;continue;}
                if(i==1&&e.type==EV_KEY){
                    unsigned bit=e.code==KEY_VOLUMEUP?FIRE:e.code==KEY_VOLUMEDOWN?USE:0;
                    if(e.value)physical_mask|=bit;else physical_mask&=~bit;
                    refresh_keys();
                }else if(!i){
                    if(e.type==EV_ABS&&e.code==ABS_MT_POSITION_X){rx=e.value;have_x=1;}
                    if(e.type==EV_ABS&&e.code==ABS_MT_POSITION_Y){ry=e.value;have_y=1;}
                    if(e.type==EV_KEY&&e.code==BTN_TOUCH){down=e.value!=0;if(!down){touch_mask=0;refresh_keys();}}
                    if(e.type==EV_SYN&&e.code==SYN_REPORT&&down&&have_x&&have_y){
                        int x=touch_canvas(rx,ax.minimum,ax.maximum,480,0,480,480);
                        int y=touch_canvas(ry,ay.minimum,ay.maximum,800,0,800,800);
                        if(x>=362&&x<475&&y>=370&&y<418){stopping=1;return;}
                        touch_mask=x<0||y<0?0:touch_keys(x,y);refresh_keys();
                    }
                }
            }
        }
        if(bytes<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK){stopping=1;return;}
    }
}
void DG_Init(void){
    epoch=millis();if(headless)return;
    fb_fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);if(fb_fd<0)fail("framebuffer");
    struct fb_var_screeninfo v;struct fb_fix_screeninfo f;
    if(ioctl(fb_fd,FBIOGET_VSCREENINFO,&v)||ioctl(fb_fd,FBIOGET_FSCREENINFO,&f)||
       v.xres!=480||v.yres!=800||v.bits_per_pixel!=16||f.line_length!=960||v.xoffset||
       v.red.offset!=11||v.red.length!=5||v.green.offset!=5||v.green.length!=6||v.blue.offset||v.blue.length!=5||
       (uint64_t)(v.yoffset+800)*960>f.smem_len){errno=EINVAL;fail("display format");}
    fb_bytes=f.smem_len;fb_offset=(size_t)v.yoffset*960;
    fb=mmap(NULL,fb_bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fb_fd,0);if(fb==MAP_FAILED){fb=NULL;fail("display mapping");}
    glob_t paths={0};
    if(glob("/dev/input/event*",GLOB_NOSORT,NULL,&paths))fail("input devices");
    for(size_t i=0;i<paths.gl_pathc;i++){
        int fd=open(paths.gl_pathv[i],O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
        char found[64]={0};(void)ioctl(fd,EVIOCGNAME(sizeof(found)),found);
        int index=!strcmp(found,"hyn_ts")?0:!strcmp(found,"jz adc keyboard")?1:-1;
        if(index<0){close(fd);continue;}
        if(inputs[index]>=0){close(fd);errno=EEXIST;fail("ambiguous game input");}
        inputs[index]=fd;
    }
    globfree(&paths);
    if(inputs[0]<0||
       ioctl(inputs[0],EVIOCGABS(ABS_MT_POSITION_X),&ax)||ioctl(inputs[0],EVIOCGABS(ABS_MT_POSITION_Y),&ay))fail("touchscreen");
    (void)touch_calibrate("","hyn_ts",&ax,&ay,480,800);
    if(ioctl(inputs[0],EVIOCGRAB,1))fail("exclusive touchscreen");
    if(inputs[1]<0||ioctl(inputs[1],EVIOCGRAB,1))fail("volume action buttons");
    controls();
}
static void save_frame(void){
    if(!output)return;
    FILE *file=fopen(output,"wb");if(!file)fail("frame output");
    fprintf(file,"P6\n320 200\n255\n");
    for(unsigned i=0;i<320*200;i++){uint32_t c=DG_ScreenBuffer[i];unsigned char bytes[]={c>>16,c>>8,c};if(fwrite(bytes,1,3,file)!=3)fail("frame write");}
    if(fclose(file))fail("frame close");
}
void DG_DrawFrame(void){
    if(headless){if(++frames>=frame_limit){save_frame();stopping=1;}return;}
    /* Correct original 320x200 pixels to a 4:3 viewport, with controls below. */
    for(int y=0;y<360;y++)for(int x=0;x<480;x++){
        uint32_t c=DG_ScreenBuffer[(y*200/360)*320+x*320/480];
        point(x,y,COLOUR((c>>16)&255,(c>>8)&255,c&255));
    }
    /* Engine startup/wipes may clear the old 400-line display area. Keep
     * the menu/exit row visible after every completed game frame. */
    topbar();
#ifdef __mips__
    if(cacheflush(fb,(int)fb_bytes,DCACHE))stopping=1;
#endif
    frames++;
}
void DG_SleepMs(uint32_t ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000};while(nanosleep(&t,&t)&&errno==EINTR&&!stopping){}}
uint32_t DG_GetTicksMs(void){return (uint32_t)(millis()-epoch);}
int DG_GetKey(int *pressed,unsigned char *key){
    if(!headless)poll_input();
    if(qread==qwrite)return 0;
    unsigned value=queue[qread];qread=(qread+1)%64;*pressed=(value>>8)&1;*key=value&255;return 1;
}
void DG_SetWindowTitle(const char *title){(void)title;}
static int self_test(void){
    if(touch_keys(125,470)!=UP||touch_keys(10,470)!=(UP|LEFT)||touch_keys(350,480)!=FIRE||touch_keys(350,600)!=USE)return 1;
    headless=1;touch_mask=FIRE;refresh_keys();physical_mask=FIRE;touch_mask=0;refresh_keys();
    int pressed;unsigned char key;
    if(!DG_GetKey(&pressed,&key)||!pressed||key!=D_FIRE||DG_GetKey(&pressed,&key))return 2;
    physical_mask=0;refresh_keys();if(!DG_GetKey(&pressed,&key)||pressed||key!=D_FIRE)return 3;
    touch_mask=UP|RIGHT;refresh_keys();touch_mask=0;refresh_keys();
    for(int i=0;i<4;i++)if(!DG_GetKey(&pressed,&key))return 4;
    if(DG_GetKey(&pressed,&key))return 5;
    players[0].readyweapon=wp_fist;players[0].weaponowned[wp_fist]=players[0].weaponowned[wp_pistol]=1;
    touch_mask=WEAPON;refresh_keys();if(!DG_GetKey(&pressed,&key)||!pressed||key!='2')return 6;
    players[0].readyweapon=wp_pistol;touch_mask=0;refresh_keys();if(!DG_GetKey(&pressed,&key)||pressed||key!='2')return 7;
    touch_mask=WEAPON;refresh_keys();if(!DG_GetKey(&pressed,&key)||!pressed||key!='1')return 8;
    return 0;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--self-test"))return self_test();
    if(argc>2&&!strcmp(argv[1],"--headless")){
        if(argc<6)return 2;
        headless=1;frame_limit=atoi(argv[2]);output=argv[3];
        if(frame_limit<1||frame_limit>10000)return 2;
        argv[3]=argv[0];argv+=3;argc-=3;
    }
    atexit(cleanup);signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);signal(SIGHUP,signal_stop);
    doomgeneric_Create(argc,argv);
    while(!stopping)doomgeneric_Tick();
    return 0;
}
