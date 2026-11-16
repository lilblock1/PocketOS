/* PocketOS: tiny event-driven framebuffer shell. No external GUI libraries. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define W 240
#define H 320
#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
static uint16_t canvas[W*H];
static uint16_t bg=RGB(13,19,24), card=RGB(25,35,43), edge=RGB(47,62,70),
    fg=RGB(239,246,246), muted=RGB(151,171,179), mint=RGB(109,226,182);
enum page { HOME, MUSIC, BOOKS, GAMES, FILES, SETTINGS, DIAGNOSTICS, DISPLAY, SOUND, RADIO, POWER, STORAGE, UPDATES, PREFERENCES, CONTROL };
static enum page page=HOME, return_page=HOME;
static int return_selected=0, clock24=0, theme=0, readable=0, saver=0, save_error=0;
static const char *config_path=NULL;
static int wifi_present=0, bt_present=0, brightness_pct=-1;
static int timezone_choice=0, allow_backlight=0, brightness_error=0, audio_present=0;
static int internal_pct=-1, sd_pct=-1;
static unsigned long long internal_free=0, sd_free=0;
static char date_label[32]="DATE UNKNOWN";
static char backlight_dir[1024]="";
static const char *const zone_names[]={"DEVICE","UTC","US EAST","US CENTRAL","US MOUNTAIN","US PACIFIC"};
static const char *const zone_rules[]={NULL,"UTC0","EST5EDT,M3.2.0/2,M11.1.0/2","CST6CDT,M3.2.0/2,M11.1.0/2","MST7MDT,M3.2.0/2,M11.1.0/2","PST8PDT,M3.2.0/2,M11.1.0/2"};
static char *original_tz=NULL;
static void apply_timezone(void) {
    const char *z=timezone_choice?zone_rules[timezone_choice]:original_tz;
    if(z){if(setenv("TZ",z,1)!=0)return;}
    else if(unsetenv("TZ")!=0)return;
    tzset();
}
static int selected=0, battery=-1;
static int volume=25, output=0;
static const char *const outputs[]={"HEADPHONES","USB AUDIO","BLUETOOTH"};
static char clock_label[16]="--:--";
static const char *titles[]={"HOME","MUSIC","BOOKS","GAMES","FILES","SETTINGS","SYSTEM","DISPLAY","SOUND","RADIOS","BATTERY","STORAGE","UPDATES","PREFERENCES","CONTROL"};
static const enum page home_items[]={MUSIC,BOOKS,GAMES,FILES,SETTINGS,DIAGNOSTICS};
static const enum page setting_items[]={DISPLAY,SOUND,RADIO,POWER,STORAGE,UPDATES,PREFERENCES};
static const unsigned char glyph[][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}
};
static void palette(void) {
    bg=RGB(13,19,24); card=RGB(25,35,43); edge=RGB(47,62,70);
    fg=RGB(239,246,246); muted=RGB(151,171,179); mint=RGB(109,226,182);
    if(theme==1) {
        bg=RGB(234,243,245);card=RGB(250,253,253);edge=RGB(200,220,224);
        fg=RGB(13,29,34);muted=RGB(50,78,85);mint=RGB(0,100,80);
    } else if(theme==2) {
        bg=RGB(0,0,0);card=RGB(20,20,20);edge=RGB(68,68,68);
        fg=RGB(255,255,255);muted=RGB(220,220,220);mint=RGB(240,224,80);
    }
    if(readable)muted=fg;
}
/* Optional caller-selected writable file. No default write to internal storage. */
static void load_preferences(void) {
    if(!config_path)return;
    int fd=open(config_path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(fd<0)return;
    struct stat st;
    if(fstat(fd,&st)<0||!S_ISREG(st.st_mode)||st.st_size>128){close(fd);return;}
    FILE *f=fdopen(fd,"r");if(!f){close(fd);return;}
    int a,b,c,d,z=0,v=25,o=0;char extra,version[16];
    int n=0;
    if(fscanf(f,"%15s",version)==1) {
        if(!strcmp(version,"POCKETOS1")) n=fscanf(f,"%d %d %d %d %c",&a,&b,&c,&d,&extra)==4;
        if(!strcmp(version,"POCKETOS2")) n=fscanf(f,"%d %d %d %d %d %c",&a,&b,&c,&d,&z,&extra)==5;
        if(!strcmp(version,"POCKETOS3")) n=fscanf(f,"%d %d %d %d %d %d %d %c",&a,&b,&c,&d,&z,&v,&o,&extra)==7;
    }
    fclose(f);
    if(n&&a>=0&&a<=2&&b>=0&&b<=1&&c>=0&&c<=1&&d>=0&&d<=1&&z>=0&&z<6&&v>=0&&v<=100&&o>=0&&o<3){
        theme=a;clock24=b;readable=c;saver=d;timezone_choice=z;volume=v;output=o;apply_timezone();
    }
}
static int save_preferences(void) {
    if(!config_path)return 1;
    char tmp[1024];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",config_path);
    if(n<0||(size_t)n>=sizeof(tmp))return 0;
    int fd=mkstemp(tmp);if(fd<0)return 0;
    FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);return 0;}
    int ok=fprintf(f,"POCKETOS3 %d %d %d %d %d %d %d\n",theme,clock24,readable,saver,timezone_choice,volume,output)>0;
    if(fflush(f)!=0||fsync(fd)!=0)ok=0;
    if(fclose(f)!=0)ok=0;
    if(ok&&rename(tmp,config_path)==0)return 1;
    unlink(tmp);return 0;
}
static void rect(int x,int y,int w,int h,uint16_t col) {
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>W?W:x+w, y1=y+h>H?H:y+h;
    for(int j=y0;j<y1;j++) for(int i=x0;i<x1;i++) canvas[j*W+i]=col;
}
static void rounded(int x,int y,int w,int h,uint16_t col) {
    rect(x+3,y,w-6,h,col); rect(x,y+3,w,h-6,col);
    rect(x+1,y+1,w-2,2,col); rect(x+1,y+h-3,w-2,2,col);
}
static void text_at(int x,int y,const char *s,int scale,uint16_t col) {
    for(;*s;s++,x+=6*scale) {
        if(x+5*scale>W) break;
        unsigned char ch=(unsigned char)toupper((unsigned char)*s);
        const unsigned char *g=NULL;
        if(ch>='A'&&ch<='Z') g=glyph[ch-'A'];
        else if(ch>='0'&&ch<='9') g=glyph[26+ch-'0'];
        if(g) for(int j=0;j<7;j++) for(int i=0;i<5;i++)
            { if(g[j]&(1u<<(4-i))) rect(x+i*scale,y+j*scale,scale,scale,col); }
        else if(ch=='-') rect(x,y+3*scale,5*scale,scale,col);
        else if(ch=='.') rect(x+2*scale,y+6*scale,scale,scale,col);
        else if(ch==':') {rect(x+2*scale,y+2*scale,scale,scale,col);rect(x+2*scale,y+5*scale,scale,scale,col);}
        else if(ch=='%') {for(int i=0;i<5;i++)rect(x+i*scale,y+(5-i)*scale,scale,scale,col);rect(x,y,scale,scale,col);rect(x+4*scale,y+6*scale,scale,scale,col);}
    }
}
static void icon(int x,int y,enum page item) {
    uint16_t col=mint;
    switch(item) {
    case MUSIC: rect(x+15,y,3,21,col);rect(x+3,y+19,15,8,col);rect(x+15,y,13,3,col);break;
    case BOOKS: rect(x,y+3,12,22,col);rect(x+15,y+3,12,22,col);rect(x+12,y+5,3,22,col);break;
    case GAMES: rounded(x,y+5,32,22,col);rect(x+5,y+10,3,12,bg);rect(x+1,y+14,11,3,bg);rect(x+22,y+12,4,4,bg);rect(x+26,y+18,4,4,bg);break;
    case FILES: rounded(x,y+7,30,20,col);rect(x+3,y+3,12,7,col);break;
    case SETTINGS: for(int i=0;i<3;i++){rect(x,y+4+i*9,29,2,col);rect(x+5+(i%2)*12,y+1+i*9,5,8,col);}break;
    default: rect(x,y+14,5,12,col);rect(x+10,y+8,5,18,col);rect(x+20,y,5,26,col);break;
    }
}
/* The preview clock is deterministic; native mode uses the system's local time. */
static void format_clock(const struct tm *t,char out[16]) {
    if(!t||t->tm_hour<0||t->tm_hour>23||t->tm_min<0||t->tm_min>59) {
        snprintf(out,16,"--:--");
        return;
    }
    if(clock24){snprintf(out,16,"%02d:%02d",t->tm_hour,t->tm_min);return;}
    int h=t->tm_hour%12;
    snprintf(out,16,"%d:%02d %s",h?h:12,t->tm_min,t->tm_hour<12?"AM":"PM");
}
static int update_clock(int *changed) {
    time_t now=time(NULL);
    struct tm t;
    char next[16];
    int valid=now!=(time_t)-1&&localtime_r(&now,&t)!=NULL&&t.tm_year>=120;
    format_clock(valid?&t:NULL,next);
    char date[32]="DATE UNKNOWN";
    if(valid&&strftime(date,sizeof(date),"%Y-%m-%d",&t)==0)snprintf(date,sizeof(date),"DATE UNKNOWN");
    *changed=strcmp(next,clock_label)!=0||strcmp(date,date_label)!=0;
    if(strcmp(date,date_label))memcpy(date_label,date,strlen(date)+1);
    if(*changed)memcpy(clock_label,next,strlen(next)+1);
    /* Reuse the existing <=30 second wait and align it with the next minute. */
    if(valid&&t.tm_sec>=0&&t.tm_sec<60) {
        int ms=(60-t.tm_sec)*1000;
        int ceiling=saver?60000:30000;
        return ms<ceiling?ms:ceiling;
    }
    return saver?60000:30000;
}
static void statusbar(void) {
    text_at(12,10,"POCKETOS",1,muted);
    text_at((W-(int)strlen(clock_label)*6)/2,10,clock_label,1,fg);
    char b[16]; if(battery>=0) snprintf(b,sizeof(b),"%d%%",battery); else snprintf(b,sizeof(b),"--%%");
    text_at(191,10,b,1,mint);
    rect(220,9,12,8,edge);rect(232,11,2,4,edge);
    if(battery>=0) rect(222,11,battery*8/100,4,mint);
    rect(12,25,216,1,edge);
}
static void row(int i,const char *label) {
    int y=80+i*29;
    rounded(12,y,216,26,i==selected?edge:card);
    if(i==selected)rect(12,y+5,2,18,mint);
    text_at(21,y+7,label,2,fg);text_at(216,y+11,"-",1,mint);
}
static int attr_path(char path[1200],const char *dir,const char *name) {
    int n=snprintf(path,1200,"%s/%s",dir,name);
    return n>0&&n<1200;
}
static long read_number(const char *dir,const char *name) {
    char path[1200],buf[64],*end;
    if(!attr_path(path,dir,name))return -1;
    FILE *f=fopen(path,"r");if(!f)return -1;
    int ok=fgets(buf,sizeof(buf),f)!=NULL;fclose(f);
    if(!ok)return -1;
    errno=0;long n=strtol(buf,&end,10);
    if(end==buf||errno)return -1;
    while(isspace((unsigned char)*end))end++;
    return *end||n<0||n>1000000?-1:n;
}
static int write_backlight(const char *dir,int percent) {
    if(!allow_backlight||!dir[0]||percent<10||percent>100)return 0;
    long max=read_number(dir,"max_brightness");
    if(max<1)return 0;
    long value=(max*percent+99)/100;
    char path[1200],buf[32];
    if(!attr_path(path,dir,"brightness"))return 0;
    int fd=open(path,O_WRONLY|O_CLOEXEC|O_NOFOLLOW|O_TRUNC);
    if(fd<0)return 0;
    int n=snprintf(buf,sizeof(buf),"%ld\n",value);
    int ok=n>0&&n<(int)sizeof(buf)&&write(fd,buf,(size_t)n)==n;
    if(close(fd)!=0)ok=0;
    return ok&&read_number(dir,"brightness")==value;
}
static int change_brightness(int pct) {
    if(pct<10)pct=10;
    if(pct>100)pct=100;
    brightness_error=!write_backlight(backlight_dir,pct);
    if(!brightness_error){
        long max=read_number(backlight_dir,"max_brightness"),val=read_number(backlight_dir,"brightness");
        if(max>0&&val>=0&&val<=max)brightness_pct=(int)(val*100/max);
    }
    return !brightness_error;
}
static int mounted_usage(const char *mount,int *pct,unsigned long long *free_mib) {
    FILE *f=fopen("/proc/mounts","r");if(!f)return 0;
    char line[1024],path[256],source[256],type[64];int found=0;
    while(fgets(line,sizeof(line),f)) {
        if(sscanf(line,"%255s %255s %63s",source,path,type)==3&&!strcmp(path,mount)){
            found=1;break;
        }
    }
    fclose(f);
    struct statvfs v;
    if(!found||statvfs(mount,&v)!=0||!v.f_blocks||v.f_bfree>v.f_blocks||!v.f_frsize)return 0;
    *pct=(int)(100.0*(double)(v.f_blocks-v.f_bfree)/(double)v.f_blocks);
    *free_mib=(unsigned long long)v.f_bavail*v.f_frsize/1048576ULL;
    return 1;
}
static void usage_text(int y,const char *name,int pct,unsigned long long free_mib) {
    char b[48];
    text_at(21,y,name,2,fg);
    if(pct<0){text_at(21,y+23,"NOT MOUNTED / UNKNOWN",1,muted);return;}
    snprintf(b,sizeof(b),"%d%% USED  %llu MB FREE",pct,free_mib);
    text_at(21,y+23,b,1,muted);
    rect(21,y+37,198,8,edge);rect(21,y+37,198*pct/100,8,mint);
}
static void quick_tile(int i,const char *label,const char *value,int active) {
    int x=12+(i%2)*110,y=82+(i/2)*61;
    rounded(x,y,106,55,i==selected?edge:card);
    if(i==selected)rect(x,y+5,2,45,mint);
    text_at(x+8,y+9,label,1,fg);
    text_at(x+8,y+30,value,1,active?mint:muted);
}
static void render(void) {
    palette();
    rect(0,0,W,H,bg);statusbar();
    text_at(12,37,page==CONTROL?"QUICK CONTROLS":titles[page],page==CONTROL||page==PREFERENCES?2:3,fg);
    if(page==CONTROL) {
        char v[16];
        text_at(12,64,"SWIPE UP OR TAP CLOSE",1,muted);
        quick_tile(0,"WI-FI",wifi_present?"DETECTED":"UNAVAILABLE",wifi_present);
        quick_tile(1,"BLUETOOTH",bt_present?"DETECTED":"UNAVAILABLE",bt_present);
        if(brightness_pct>=0)snprintf(v,sizeof(v),"%d%%",brightness_pct);
        else snprintf(v,sizeof(v),"UNAVAILABLE");
        quick_tile(2,"BRIGHTNESS",v,brightness_pct>=0);
        snprintf(v,sizeof(v),"%d%% TARGET",volume);quick_tile(3,"VOLUME",v,0);
        quick_tile(4,"BATTERY SAVER",saver?"PREFERRED":"STANDARD",saver);
        quick_tile(5,"SETTINGS","OPEN",1);
        text_at(12,270,"RADIO / AUDIO WRITES LOCKED",1,muted);
        text_at(12,301,"CLOSE",2,mint);
    } else if(page==PREFERENCES) {
        text_at(12,64,"TAP TO CHANGE",1,muted);
        row(0,theme==0?"THEME: DARK":theme==1?"THEME: LIGHT":"HIGH CONTRAST");
        row(1,clock24?"CLOCK: 24H":"CLOCK: 12H");
        row(2,readable?"TEXT: CONTRAST":"TEXT: NORMAL");
        row(3,saver?"POWER: SAVER":"POWER: NORMAL");
        char z[32];snprintf(z,sizeof(z),"TZ: %s",zone_names[timezone_choice]);row(4,z);
        text_at(12,234,date_label,1,muted);
        text_at(12,251,save_error?"SAVE FAILED":config_path?"SETTINGS SAVED ON CHANGE":"SESSION ONLY",1,muted);
        text_at(12,301,"BACK",2,mint);
    } else if(page==HOME) {
        text_at(12,64,"YOUR POCKET. YOUR WORLD.",1,muted);
        for(int i=0;i<6;i++) {
            int x=12+(i%2)*110,y=80+(i/2)*66;
            rounded(x,y,106,60,i==selected?edge:card);
            if(i==selected)rect(x,y+5,2,50,mint);
            icon(x+10,y+8,home_items[i]);text_at(x+10,y+41,titles[home_items[i]],1,fg);
        }
        text_at(12,301,"ALPHA  /  DEVICE SHELL",1,muted);
    } else if(page==SETTINGS) {
        text_at(12,64,"DEVICE PREFERENCES",1,muted);
        for(int i=0;i<7;i++) row(i,titles[setting_items[i]]);
        text_at(12,301,"BACK",2,mint);
    } else if(page==DISPLAY){
        char b[16];
        rounded(12,81,216,122,card);
        text_at(21,93,"BRIGHTNESS",2,fg);
        if(brightness_pct>=0)snprintf(b,sizeof(b),"%d%%",brightness_pct);
        else snprintf(b,sizeof(b),"UNKNOWN");
        text_at(21,119,b,3,mint);
        rect(24,160,192,12,edge);
        if(brightness_pct>=0)rect(24,160,192*brightness_pct/100,12,mint);
        text_at(21,188,brightness_error?"WRITE FAILED":allow_backlight?"TAP BAR TO ADJUST":"READ ONLY - LOCKED",1,muted);
        text_at(12,301,"BACK",2,mint);
    } else if(page==STORAGE){
        rounded(12,81,216,170,card);
        usage_text(94,"INTERNAL DATA",internal_pct,internal_free);
        usage_text(174,"MICROSD",sd_pct,sd_free);
        text_at(12,268,"NO FORMAT OR MOUNT ACTIONS",1,muted);
        text_at(12,301,"BACK",2,mint);
    } else if(page==SOUND){
        char b[16];snprintf(b,sizeof(b),"%d%%",volume);
        rounded(12,81,216,122,card);
        text_at(21,93,"VOLUME TARGET",2,fg);
        text_at(21,119,b,3,mint);
        rect(24,160,192,12,edge);rect(24,160,192*volume/100,12,mint);
        text_at(21,188,"PLAYBACK NOT CONNECTED",1,muted);
        rounded(12,213,216,49,card);
        text_at(21,223,"OUTPUT PREFERENCE",1,muted);
        text_at(21,239,outputs[output],2,fg);
        text_at(12,301,"BACK",2,mint);
    } else if(page==RADIO){
        rounded(12,81,216,125,card);
        if(page==RADIO){
            text_at(21,94,wifi_present?"WI-FI: DETECTED":"WI-FI: UNKNOWN",2,fg);
            text_at(21,117,bt_present?"BT: DETECTED":"BT: UNKNOWN",2,fg);
            text_at(21,149,"BACKEND CLI AVAILABLE",1,muted);
        } else {
            text_at(21,94,"AUDIO OUTPUT",2,fg);
            text_at(21,122,audio_present?"PCM DEVICE DETECTED":"PCM NOT DETECTED",1,mint);
            text_at(21,149,"MIXER NOT VERIFIED",1,muted);
        }
        text_at(21,174,"UI CONNECTION PENDING",1,muted);
        text_at(12,301,"BACK",2,mint);
    } else {
        rounded(12,81,216,92,card);
        if(page==DIAGNOSTICS) {
            text_at(21,94,"R1 DEVELOPMENT",2,mint);
            text_at(21,119,"FRAMEBUFFER SHELL",1,fg);
            text_at(21,133,"HARDWARE NOT VERIFIED",1,muted);
            text_at(21,148,"INSTALLATION DISABLED",1,muted);
        } else if(page==POWER) {
            char b[16]; if(battery>=0)snprintf(b,sizeof(b),"%d%%",battery);else snprintf(b,sizeof(b),"UNKNOWN");
            text_at(21,94,"BATTERY",2,mint);text_at(21,122,b,3,fg);
            text_at(21,154,"READ ONLY",1,muted);
        } else {
            text_at(21,94,"COMING NEXT",2,mint);
            text_at(21,121,"SERVICE NOT CONNECTED",1,fg);
            text_at(21,137,"NO HARDWARE CHANGES",1,muted);
        }
        text_at(12,301,"BACK",2,mint);
    }
}
static void open_control(void) {
    if(page==CONTROL)return;
    return_page=page;return_selected=selected;page=CONTROL;selected=0;
}
static void back(void) {
    if(page==CONTROL){page=return_page;selected=return_selected;return;}
    page=(page>=DISPLAY?SETTINGS:HOME);selected=0;
}
static void activate(int i) {
    if(i<0)return;
    if(page==PREFERENCES) {
        if(i==0)theme=(theme+1)%3;
        else if(i==1)clock24=!clock24;
        else if(i==2)readable=!readable;
        else if(i==3)saver=!saver;
        else if(i==4){timezone_choice=(timezone_choice+1)%6;apply_timezone();}
        else return;
        save_error=!save_preferences();return;
    }
    if(page==CONTROL) {
        if(i==0||i==1)page=RADIO;
        else if(i==2)page=DISPLAY;
        else if(i==3)page=SOUND;
        else if(i==4){saver=!saver;save_error=!save_preferences();return;}
        else if(i==5)page=SETTINGS;
        else return;
        selected=0;return;
    }
    if(page==HOME&&i<6)page=home_items[i];
    else if(page==SETTINGS&&i<7)page=setting_items[i];else return;
    selected=0;
}
static void set_volume_target(int v){
    volume=v<0?0:v>100?100:v;
    /* Never pass a UI percentage directly to an unverified DAC register. */
    save_error=!save_preferences();
}
static void tap(int x,int y) {
    if(y<26){if(page==CONTROL)back();else open_control();return;}
    if(y>=287&&page!=HOME){back();return;}
    if(page==SOUND&&x>=24&&x<=216&&y>=148&&y<=183){set_volume_target((x-24)*100/192);return;}
    if(page==SOUND&&x>=12&&x<228&&y>=213&&y<263){output=(output+1)%3;save_error=!save_preferences();return;}
    if(page==DISPLAY&&x>=24&&x<=216&&y>=148&&y<=183&&allow_backlight){change_brightness((x-24)*100/192);return;}
    if(x<12||x>=228||y<80)return;
    if(page==HOME&&y<278) {
        int a=x-12,b=y-80;
        if(a%110<106&&b%66<60)activate((b/66)*2+a/110);
    } else if(page==CONTROL&&y>=82&&y<265) {
        int a=x-12,b=y-82;
        if(a%110<106&&b%61<55)activate((b/61)*2+a/110);
    } else if((page==SETTINGS||page==PREFERENCES)&&y<283&&(y-80)%29<26) activate((y-80)/29);
}
static int key(unsigned code) {
    int prev=selected,old_theme=theme,old_clock=clock24,old_readable=readable,old_saver=saver,old_zone=timezone_choice,old_brightness=brightness_pct;enum page prior=page;
    if(code==KEY_VOLUMEUP||code==KEY_VOLUMEDOWN||(page==SOUND&&(code==KEY_LEFT||code==KEY_RIGHT))){set_volume_target(volume+((code==KEY_VOLUMEUP||code==KEY_RIGHT)?5:-5));return 1;}
    if(page==SOUND&&(code==KEY_ENTER||code==KEY_OK)){output=(output+1)%3;save_error=!save_preferences();return 1;}
    if(code==KEY_MENU){if(page==CONTROL)back();else open_control();}
    else if(code==KEY_ESC||code==KEY_BACK)back();
    else if(code==KEY_ENTER||code==KEY_OK||code==KEY_SELECT)activate(selected);
    else if(page==DISPLAY&&allow_backlight&&brightness_pct>=0&&(code==KEY_LEFT||code==KEY_RIGHT)) {change_brightness(brightness_pct+(code==KEY_RIGHT?10:-10));return 1;}
    else if(page==HOME||page==SETTINGS||page==CONTROL||page==PREFERENCES) {
        int count=page==SETTINGS?7:page==PREFERENCES?5:6;
        int step=(page==HOME||page==CONTROL)?2:1;
        if(code==KEY_DOWN)selected=(selected+step)%count;
        else if(code==KEY_UP)selected=(selected+count-step)%count;
        else if(code==KEY_RIGHT)selected=(selected+1)%count;
        else if(code==KEY_LEFT)selected=(selected+count-1)%count;
    }
    return prev!=selected||prior!=page||old_theme!=theme||old_clock!=clock24||old_readable!=readable||old_saver!=saver||old_zone!=timezone_choice||old_brightness!=brightness_pct;
}
static int sample_battery(void) {
    glob_t g={0};int value=-1,found=0;
    if(glob("/sys/class/power_supply/*/capacity",0,NULL,&g)==0) {
        for(size_t i=0;i<g.gl_pathc;i++) {
            FILE *f=fopen(g.gl_pathv[i],"r");int n;
            if(f){if(fscanf(f,"%d",&n)==1&&n>=0&&n<=100){value=n;found++;}fclose(f);}
        }
    }
    globfree(&g);return found==1?value:-1;
}
static void sample_quick(void) {
    wifi_present=access("/sys/class/net/wlan0",F_OK)==0;
    glob_t g={0};
    bt_present=glob("/sys/class/bluetooth/hci*",GLOB_NOSORT,NULL,&g)==0&&g.gl_pathc>0;globfree(&g);
    memset(&g,0,sizeof(g));
    audio_present=glob("/dev/snd/pcmC*D*p",GLOB_NOSORT,NULL,&g)==0&&g.gl_pathc>0;globfree(&g);
    memset(&g,0,sizeof(g));
    brightness_pct=-1;backlight_dir[0]='\0';
    if(glob("/sys/class/backlight/*",0,NULL,&g)==0&&g.gl_pathc==1&&strlen(g.gl_pathv[0])<sizeof(backlight_dir)){
        long max=read_number(g.gl_pathv[0],"max_brightness");
        long v=read_number(g.gl_pathv[0],"actual_brightness");
        if(v<0)v=read_number(g.gl_pathv[0],"brightness");
        if(max>0&&v>=0&&v<=max){brightness_pct=(int)(v*100/max);strcpy(backlight_dir,g.gl_pathv[0]);}
    }
    globfree(&g);
    internal_pct=-1;sd_pct=-1;internal_free=0;sd_free=0;
    (void)mounted_usage("/usr/data",&internal_pct,&internal_free);
    (void)mounted_usage("/mnt/sd_0",&sd_pct,&sd_free);
}
static int ppm(const char *path) {
    FILE *f=fopen(path,"wb");if(!f)return 1;
    if(fprintf(f,"P6\n%d %d\n255\n",W,H)<0){fclose(f);return 1;}
    unsigned char line[W*3];
    for(int y=0;y<H;y++){
        for(int x=0;x<W;x++){uint16_t v=canvas[y*W+x];line[x*3]=(unsigned char)(((v>>11)&31)*255/31);line[x*3+1]=(unsigned char)(((v>>5)&63)*255/63);line[x*3+2]=(unsigned char)((v&31)*255/31);}
        if(fwrite(line,1,sizeof(line),f)!=sizeof(line)){fclose(f);return 1;}
    }
    return fclose(f)!=0;
}
struct fb {int fd;struct fb_var_screeninfo v;struct fb_fix_screeninfo f;unsigned char *mem;unsigned bytes,vw,vh,ox,oy;};
static int field_ok(struct fb_bitfield b,unsigned bits){return b.length>0&&b.length<=8&&b.offset<bits&&b.length<=bits-b.offset&&!b.msb_right;}
static uint32_t field_mask(struct fb_bitfield b) {
    return b.length ? ((1u<<b.length)-1)<<b.offset : 0;
}
static int fb_open(struct fb *b) {
    b->fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);if(b->fd<0)return 0;
    if(ioctl(b->fd,FBIOGET_VSCREENINFO,&b->v)<0||ioctl(b->fd,FBIOGET_FSCREENINFO,&b->f)<0)goto bad;
    unsigned bits=b->v.bits_per_pixel;
    if((bits!=16&&bits!=24&&bits!=32)||b->f.type!=FB_TYPE_PACKED_PIXELS||b->f.visual!=FB_VISUAL_TRUECOLOR||
       !field_ok(b->v.red,bits)||!field_ok(b->v.green,bits)||!field_ok(b->v.blue,bits)||
       (b->v.transp.length&&!field_ok(b->v.transp,bits))||!b->v.xres||!b->v.yres||b->v.xres>4096||b->v.yres>4096)goto bad;
    uint32_t r=field_mask(b->v.red), g=field_mask(b->v.green), blue=field_mask(b->v.blue), a=field_mask(b->v.transp);
    if((r&g)||(r&blue)||(g&blue)||(a&(r|g|blue)))goto bad;
    b->bytes=bits/8;
    if((uint64_t)b->v.xoffset+b->v.xres>b->v.xres_virtual||(uint64_t)b->v.yoffset+b->v.yres>b->v.yres_virtual||
       ((uint64_t)b->v.xoffset+b->v.xres)*b->bytes>b->f.line_length||
       ((uint64_t)b->v.yoffset+b->v.yres)*b->f.line_length>b->f.smem_len)goto bad;
    b->vw=b->v.xres;b->vh=b->vw*H/W;
    if(b->vh>b->v.yres){b->vh=b->v.yres;b->vw=b->vh*W/H;}
    if(!b->vw||!b->vh)goto bad;
    b->ox=(b->v.xres-b->vw)/2;b->oy=(b->v.yres-b->vh)/2;
    b->mem=mmap(NULL,b->f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,b->fd,0);
    if(b->mem==MAP_FAILED)goto bad;
    return 1;
bad: close(b->fd);return 0;
}
static uint32_t channel(unsigned value,unsigned max,struct fb_bitfield b) {
    return ((value*((1u<<b.length)-1)+max/2)/max)<<b.offset;
}
static void present(struct fb *b) {
    /* Draw only when a screen or displayed status changes. One bounded RGB565 canvas. */
    for(unsigned y=0;y<b->v.yres;y++){
        unsigned char *line=b->mem+((size_t)y+b->v.yoffset)*b->f.line_length+(size_t)b->v.xoffset*b->bytes;
        for(unsigned x=0;x<b->v.xres;x++){
            uint16_t c=bg;
            if(x>=b->ox&&x<b->ox+b->vw&&y>=b->oy&&y<b->oy+b->vh)c=canvas[((y-b->oy)*H/b->vh)*W+(x-b->ox)*W/b->vw];
            uint32_t pixel=channel((c>>11)&31,31,b->v.red)|channel((c>>5)&63,63,b->v.green)|channel(c&31,31,b->v.blue);
            if(b->v.transp.length)pixel|=((1u<<b->v.transp.length)-1)<<b->v.transp.offset;
            for(unsigned i=0;i<b->bytes;i++)line[x*b->bytes+i]=(unsigned char)(pixel>>(8*i));
        }
    }
}
static int native(const char *input) {
    struct fb b={0};if(!fb_open(&b)){fputs("Unsupported or unavailable framebuffer\n",stderr);return 1;}
    int fd=open(input,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0){perror("input");munmap(b.mem,b.f.smem_len);close(b.fd);return 1;}
    struct input_absinfo ax={0},ay={0};
    int mt=ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&ax)==0&&ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&ay)==0;
    int touch=mt||(ioctl(fd,EVIOCGABS(ABS_X),&ax)==0&&ioctl(fd,EVIOCGABS(ABS_Y),&ay)==0);
    touch=touch&&ax.maximum>ax.minimum&&ay.maximum>ay.minimum;
    int rx=0,ry=0,have_x=0,have_y=0,down=0,pending=0,slot=0,dropped=0;
    int start_y=-1;
    int clock_changed=0;
    (void)update_clock(&clock_changed);
    load_preferences();(void)update_clock(&clock_changed);
    battery=sample_battery();sample_quick();render();present(&b);
    for(;;){
        int timeout=update_clock(&clock_changed);
        if(clock_changed){render();present(&b);}
        struct pollfd p={.fd=fd,.events=POLLIN};int rc=poll(&p,1,timeout);
        if(rc<0){if(errno==EINTR)continue;break;}
        if(!rc){
            int n=sample_battery(),w=wifi_present,bt=bt_present,br=brightness_pct,audio=audio_present,ip=internal_pct,sp=sd_pct;
            unsigned long long i=internal_free,j=sd_free;
            sample_quick();
            if(n!=battery||w!=wifi_present||bt!=bt_present||br!=brightness_pct||audio!=audio_present||ip!=internal_pct||sp!=sd_pct||i!=internal_free||j!=sd_free){battery=n;render();present(&b);}
            continue;
        }
        if(!(p.revents&POLLIN)||(p.revents&(POLLERR|POLLHUP|POLLNVAL)))break;
        struct input_event e[16];ssize_t got=read(fd,e,sizeof(e));
        if(got<0){if(errno==EAGAIN||errno==EINTR)continue;break;}if(!got)break;
        int changed=0;
        for(size_t i=0;i<(size_t)got/sizeof(*e);i++){
            struct input_event v=e[i];
            if(v.type==EV_SYN&&v.code==SYN_DROPPED){dropped=1;pending=down=have_x=have_y=0;start_y=-1;continue;}
            if(dropped){if(v.type==EV_SYN&&v.code==SYN_REPORT)dropped=0;continue;}
            if(v.type==EV_ABS&&v.code==ABS_MT_SLOT){slot=v.value;continue;}
            if(touch&&v.type==EV_ABS&&(!mt||slot==0)){
                if(v.code==(mt?ABS_MT_POSITION_X:ABS_X)){rx=v.value;have_x=1;}
                else if(v.code==(mt?ABS_MT_POSITION_Y:ABS_Y)){ry=v.value;have_y=1;}
                else if(mt&&v.code==ABS_MT_TRACKING_ID){if(v.value>=0){down=1;have_x=have_y=0;start_y=-1;}else if(down){pending=1;down=0;}}
            }
            if(touch&&!mt&&v.type==EV_KEY&&v.code==BTN_TOUCH){if(v.value==1){down=1;start_y=-1;}else if(!v.value&&down){pending=1;down=0;}}
            if(v.type==EV_KEY&&v.value==1&&v.code!=BTN_TOUCH)changed|=key(v.code);
            if(v.type==EV_SYN&&v.code==SYN_REPORT&&down&&have_y&&start_y<0){
                int64_t p0=((int64_t)ry-ay.minimum)*(b.v.yres-1)/((int64_t)ay.maximum-ay.minimum);
                if(p0>=b.oy&&p0<(int64_t)b.oy+b.vh)start_y=(int)((p0-b.oy)*H/b.vh);
            }
            if(v.type==EV_SYN&&v.code==SYN_REPORT&&pending){
                if(have_x&&have_y){
                    int64_t px=((int64_t)rx-ax.minimum)*(b.v.xres-1)/((int64_t)ax.maximum-ax.minimum);
                    int64_t py=((int64_t)ry-ay.minimum)*(b.v.yres-1)/((int64_t)ay.maximum-ay.minimum);
                    if(px>=b.ox&&px<(int64_t)b.ox+b.vw&&py>=b.oy&&py<(int64_t)b.oy+b.vh){
                        int y=(int)((py-b.oy)*H/b.vh);
                        if(start_y>=0&&start_y<26&&y-start_y>=35&&page!=CONTROL)open_control();
                        else if(page==CONTROL&&start_y>=0&&start_y-y>=40)back();
                        else tap((int)((px-b.ox)*W/b.vw),y);
                        changed=1;
                    }
                }
                pending=0;have_x=have_y=0;start_y=-1;
            }
        }
        if(changed){(void)update_clock(&clock_changed);render();present(&b);}
    }
    close(fd);munmap(b.mem,b.f.smem_len);close(b.fd);return 1;
}
static int selftest(void) {
    char label[16];
    struct tm test={.tm_hour=0,.tm_min=5};
    format_clock(&test,label);if(strcmp(label,"12:05 AM"))return 1;
    test.tm_hour=12;format_clock(&test,label);if(strcmp(label,"12:05 PM"))return 1;
    test.tm_hour=23;test.tm_min=59;format_clock(&test,label);if(strcmp(label,"11:59 PM"))return 1;
    format_clock(NULL,label);if(strcmp(label,"--:--"))return 1;
    page=HOME;selected=0;tap(20,80);if(page!=MUSIC)return 1;back();
    tap(20,215);if(page!=SETTINGS)return 1;tap(20,180);if(page!=POWER)return 1;back();
    if(page!=SETTINGS)return 1;
    back();
    if(page!=HOME)return 1;
    key(KEY_DOWN);if(selected!=2)return 1;key(KEY_ENTER);if(page!=GAMES)return 1;
    back();tap(100,10);if(page!=CONTROL)return 1;
    tap(20,210);if(!saver||page!=CONTROL)return 1;
    tap(20,300);if(page!=HOME)return 1;
    tap(20,215);if(page!=SETTINGS)return 1;
    tap(20,260);if(page!=PREFERENCES)return 1;
    tap(20,110);if(!clock24)return 1;
    format_clock(&test,label);if(strcmp(label,"23:59"))return 1;
    tap(20,82);if(theme!=1)return 1;
    tap(20,200);if(timezone_choice!=1)return 1;
    page=SOUND;tap(120,162);if(volume!=50)return 1;
    key(KEY_VOLUMEUP);if(volume!=55)return 1;
    tap(30,230);if(output!=1)return 1;
    render();return 0;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS shell v1");return 0;}
    if(argc==2&&!strcmp(argv[1],"--self-test"))return selftest();
    if(argc==3&&!strcmp(argv[1],"--test-backlight")&&argv[2][0]=='/') {
        if(write_backlight(argv[2],50))return 1;
        allow_backlight=1;
        if(write_backlight(argv[2],0)||write_backlight(argv[2],101))return 1;
        return !write_backlight(argv[2],50);
    }
    if(argc==3&&!strcmp(argv[1],"--test-config")&&argv[2][0]=='/') {
        config_path=argv[2];theme=2;clock24=readable=saver=1;timezone_choice=3;volume=37;output=2;
        if(!save_preferences())return 1;
        theme=clock24=readable=saver=timezone_choice=volume=output=0;load_preferences();
        return !(theme==2&&clock24&&readable&&saver&&timezone_choice==3&&volume==37&&output==2);
    }
    if(argc==4&&!strcmp(argv[1],"--preview")){
        int found=0;for(unsigned i=0;i<sizeof(titles)/sizeof(titles[0]);i++)if(!strcmp(argv[2],titles[i])){page=(enum page)i;found=1;}
        if(!found)return 2;
        struct tm sample={.tm_hour=10,.tm_min=42};
        format_clock(&sample,clock_label);
        strcpy(date_label,"2026-11-15");
        render();
        return ppm(argv[3]);
    }
    if(argc>=5&&!strcmp(argv[1],"--config")&&argv[2][0]=='/') {config_path=argv[2];argc-=2;argv+=2;}
    if(argc==4&&!strcmp(argv[1],"--allow-backlight")){allow_backlight=1;argc--;argv++;}
    const char *tz=getenv("TZ");if(tz)original_tz=strdup(tz);
    if(argc==3&&!strcmp(argv[1],"--fb")&&!strncmp(argv[2],"/dev/input/event",16)) {
        const char *n=argv[2]+16;
        if(*n&&strspn(n,"0123456789")==strlen(n))return native(argv[2]);
    }
    fputs("Usage: pocketos-shell --preview HOME|SETTINGS|SYSTEM output.ppm | --self-test | --fb /dev/input/eventN\n",stderr);return 2;
}
