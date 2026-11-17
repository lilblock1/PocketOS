/* PocketOS: tiny event-driven framebuffer shell. No external GUI libraries. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE 1
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
#include <dirent.h>
#include <strings.h>
#include "pocketos-audio-formats.h"
#include "pocketos-music-tags.h"
#include "pocketos-volume.h"
#include "pocketos-bt-audio.h"
#include "pocketos-reader-font.h"
#include "pocketos-files.h"
#include "pocketos-radio-ui.h"
#include "pocketos-update-ui.h"
#include "pocketos-power.h"
#include "pocketos-suspend.h"
#include "pocketos-hardware.h"
#include "pocketos-touch.h"
#include <signal.h>
#include <sys/wait.h>
#ifdef __mips__
#include <sys/cachectl.h>
#endif

#define W 240
#define H 400
#define PIXEL_SCALE 2
#define CW (W*PIXEL_SCALE)
#define CH (H*PIXEL_SCALE)
#ifndef READER_ATLAS_WIDTH
#define READER_ATLAS_WIDTH 18
#define READER_ATLAS_HEIGHT 20
#endif
#define LAYOUT_H 320
/* Expand vertical layout spacing for the 3:5 panel; keep glyphs/icons square. */
static int layout_depth;
static int layout_y(int y){return y*H/LAYOUT_H;}
#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
static uint16_t canvas[CW*CH];
static uint16_t bg=RGB(13,19,24), card=RGB(25,35,43), edge=RGB(47,62,70),
    fg=RGB(239,246,246), muted=RGB(151,171,179), mint=RGB(109,226,182);
enum page { HOME, MUSIC, BOOKS, GAMES, FILES, SETTINGS, DIAGNOSTICS, DISPLAY, SOUND, RADIO, POWER, STORAGE, UPDATES, PREFERENCES, CONTROL, NOWPLAYING, READER, WIFIUI, BTUI };
static enum page page=HOME, return_page=HOME;
static int return_selected=0, clock24=0, theme=0, readable=0, saver=0, save_error=0;
static const char *config_path=NULL;
static int wifi_present=0, bt_present=0, brightness_pct=-1;
static int timezone_choice=0, allow_backlight=0, brightness_error=0, audio_present=0;
static int unblank_display=0;
static int brightness_saved=-1;
static int internal_pct=-1, sd_pct=-1;
static int output_busy=0;
static int media_online=-1,media_stale;
static pid_t sd_worker=-1;
static dev_t media_dev;
static ino_t media_ino;
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
static int allow_audio=0, music_count=0, music_index=0, music_top=0, music_pause=0, music_error=0, music_fd=-1;
static pid_t music_pid=-1;
#define TRACK_CAP 512
static char music_files[TRACK_CAP][256];
static int music_report=-1,music_shuffle=0,music_repeat=0,music_list=0;
static uint32_t music_pos=0,music_length=0,music_start_at=0;
static char track_title[65],track_artist[65],track_album[65];
static unsigned char cover[64*64*3];
static int cover_ok=0;
static unsigned music_random=0x8b21a37u;
static int music_pending=-1, music_manual_stop=0,music_route=-1,music_switch=0,music_seek_paused=0;
#define BOOK_CAP 128
#define BOOK_ROWS 18
#define BOOK_HISTORY 256
static char books[BOOK_CAP][256],book_text[BOOK_ROWS][65],book_status[38];
static int book_indent[BOOK_ROWS],book_bold[BOOK_ROWS],book_y[BOOK_ROWS],book_render_rows,book_reflow;
static int book_count=0,book_index=0,book_top=0,book_size=1,book_wide=0,book_eof=0,book_depth=0;
static int book_theme=3,book_font=0,book_font_ok=0,book_menu=0,book_allow_save=0,book_dirty=0,book_save_error=0;
static int book_allow_prepare=0;
static pid_t book_prepare_pid=-1;
static char book_prepare_name[256];
static unsigned char (*book_atlas)[16][12]=NULL;
static unsigned char *book_native_pixels;
static unsigned char book_native_advance[2][95];
static unsigned book_native_width,book_native_height;
static char book_title[65];
static long long book_mtime=0;
static off_t book_page_span=0;
static off_t book_offsets[BOOK_HISTORY],book_pos=0,book_next=0,book_bytes=0;
static const char *book_dir="/mnt/sd_0/Books";
static FILE *book_file=NULL;
static int fm_allow=0,fm_panel=0,fm_confirm=0,fm_preview=0,fm_edit_new=0,fm_action=0;
static char fm_message[40],fm_rename[64];
static pid_t fm_worker=-1;
static int fm_worker_fd=-1,fm_worker_action;
static unsigned fm_percent;
static const char *music_dir="/mnt/sd_0/Music";
static int volume=25, output=3,volume_extended=0,volume_warning=0;
static int music_keep_audio(int route,int jack);
static const char *const outputs[]={"HEADPHONES","USB AUDIO","BLUETOOTH","AUTO"};
static char clock_label[16]="--:--";
static const char *titles[]={"HOME","MUSIC","BOOKS","GAMES","FILES","SETTINGS","SYSTEM","DISPLAY","SOUND","RADIOS","BATTERY","STORAGE","UPDATES","PREFERENCES","CONTROL","NOW PLAYING","READER","WI-FI","BLUETOOTH"};
static const enum page home_items[]={MUSIC,BOOKS,FILES,SETTINGS,CONTROL,DIAGNOSTICS};
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
    bg=RGB(250,251,253); card=RGB(232,236,242); edge=RGB(198,207,219);
    fg=RGB(24,32,45); muted=RGB(91,105,125); mint=RGB(34,91,177);
    if(theme==1) {
        bg=RGB(18,24,34);card=RGB(32,42,57);edge=RGB(67,82,104);
        fg=RGB(243,246,251);muted=RGB(166,181,202);mint=RGB(58,112,194);
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
    int a,b,c,d,z=0,v=25,o=0,br=-1,sh=0,rp=0,pl=0;char extra,version[16];
    int n=0;
    if(fscanf(f,"%15s",version)==1) {
        if(!strcmp(version,"POCKETOS1")) n=fscanf(f,"%d %d %d %d %c",&a,&b,&c,&d,&extra)==4;
        if(!strcmp(version,"POCKETOS2")) n=fscanf(f,"%d %d %d %d %d %c",&a,&b,&c,&d,&z,&extra)==5;
        if(!strcmp(version,"POCKETOS3")) n=fscanf(f,"%d %d %d %d %d %d %d %c",&a,&b,&c,&d,&z,&v,&o,&extra)==7;
        if(!strcmp(version,"POCKETOS4")||!strcmp(version,"POCKETOS5")) n=fscanf(f,"%d %d %d %d %d %d %d %d %d %d %d %c",&a,&b,&c,&d,&z,&v,&o,&br,&sh,&rp,&pl,&extra)==11;
        if(n&&strcmp(version,"POCKETOS5")&&o==0)o=3;
    }
    fclose(f);
    if(n&&a>=0&&a<=2&&b>=0&&b<=1&&c>=0&&c<=1&&d>=0&&d<=1&&z>=0&&z<6&&v>=0&&v<=100&&o>=0&&o<4&&
       (br==-1||(br>=10&&br<=100))&&sh>=0&&sh<=1&&rp>=0&&rp<=2&&pl>=0&&pl<=1){
        brightness_saved=br;music_shuffle=sh;music_repeat=rp;music_list=pl;
        theme=a;clock24=b;readable=c;saver=d;timezone_choice=z;volume=v>VOL_BOOT_MAX?VOL_BOOT_MAX:v;volume_extended=volume_warning=0;output=o;apply_timezone();
    }
}
static int save_preferences(void) {
    if(!config_path)return 1;
    char tmp[1024];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",config_path);
    if(n<0||(size_t)n>=sizeof(tmp))return 0;
    int fd=mkstemp(tmp);if(fd<0)return 0;
    FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);return 0;}
    int ok=fprintf(f,"POCKETOS5 %d %d %d %d %d %d %d %d %d %d %d\n",theme,clock24,readable,saver,timezone_choice,vol_clamp(volume,0),output,brightness_saved,music_shuffle,music_repeat,music_list)>0;
    if(fflush(f)!=0||fsync(fd)!=0)ok=0;
    if(fclose(f)!=0)ok=0;
    if(ok&&rename(tmp,config_path)==0){
        /* Make the atomic replacement durable across a cold power cycle. */
        char dir[1024];size_t len=strlen(config_path);
        if(len>=sizeof(dir))return 0;
        memcpy(dir,config_path,len+1);char *slash=strrchr(dir,'/');
        if(!slash)strcpy(dir,".");else if(slash==dir)slash[1]=0;else *slash=0;
        int parent=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(parent<0)return 0;
        int durable=fsync(parent)==0;if(close(parent)!=0)durable=0;
        return durable;
    }
    unlink(tmp);return 0;
}
static void rect(int x,int y,int w,int h,uint16_t col) {
    if(!layout_depth){int bottom=layout_y(y+h);y=layout_y(y);h=bottom-y;}
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>W?W:x+w, y1=y+h>H?H:y+h;
    for(int j=y0*PIXEL_SCALE;j<y1*PIXEL_SCALE;j++) for(int i=x0*PIXEL_SCALE;i<x1*PIXEL_SCALE;i++) canvas[j*CW+i]=col;
}
static void rounded(int x,int y,int w,int h,uint16_t col) {
    rect(x,y,w,h,col);
}
static void text_at(int x,int y,const char *s,int scale,uint16_t col);
/* Original, small mixed-case bitmap glyphs for the reader. */
static const unsigned char lower[][7]={
 {0,0,14,1,15,17,15},{16,16,22,25,17,17,30},{0,0,14,17,16,17,14},
 {1,1,13,19,17,17,15},{0,0,14,17,31,16,14},{6,9,8,28,8,8,8},
 {0,0,15,17,15,1,14},{16,16,22,25,17,17,17},{4,0,12,4,4,4,14},
 {2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
 {0,0,26,21,21,21,21},{0,0,22,25,17,17,17},{0,0,14,17,17,17,14},
 {0,0,30,17,30,16,16},{0,0,15,17,15,1,1},{0,0,22,25,16,16,16},
 {0,0,15,16,14,1,30},{8,8,28,8,8,9,6},{0,0,17,17,17,19,13},
 {0,0,17,17,17,10,4},{0,0,17,17,21,21,10},{0,0,17,10,4,10,17},
 {0,0,17,17,15,1,14},{0,0,31,2,4,8,31}
};
static const unsigned char punct[][7]={
 {0,0,0,0,0,0,4},{0,0,0,0,0,4,8},{4,4,4,4,4,0,4},
 {14,17,1,2,4,0,4},{0,4,0,0,4,0,0},{0,4,0,0,4,4,8},
 {0,0,0,14,0,0,0},{4,4,8,0,0,0,0},{10,10,20,0,0,0,0},
 {2,4,8,8,8,4,2},{8,4,2,2,2,4,8},{1,2,2,4,8,8,16},
 {0,4,4,31,4,4,0},{0,0,0,31,0,0,0},{17,2,4,8,16,17,0},
 {14,17,18,12,21,18,13}
};
static const unsigned char *book_glyph(unsigned char ch){
    const char *symbols=".,!?:;-\'\"()/+=%&";
    if(ch>='a'&&ch<='z')return lower[ch-'a'];
    if(ch>='A'&&ch<='Z')return glyph[ch-'A'];
    if(ch>='0'&&ch<='9')return glyph[26+ch-'0'];
    const char *p=strchr(symbols,(int)ch);
    if(ch&&p)return punct[p-symbols];
    return NULL;
}
static uint16_t blend565(uint16_t paper,uint16_t ink,unsigned a){
    unsigned r=(((paper>>11)&31)*(255-a)+((ink>>11)&31)*a)/255;
    unsigned g=(((paper>>5)&63)*(255-a)+((ink>>5)&63)*a)/255;
    unsigned b=((paper&31)*(255-a)+(ink&31)*a)/255;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static void reader_draw(int x,int y,const char *text,int height,uint16_t ink,uint16_t paper,int custom,int bold,int limit,int overlay);
static void book_print(int x,int y,const char *text,int width,int height,uint16_t ink,uint16_t paper,int atlas){
    if(!atlas||book_font_ok!=1){reader_draw(x,y,text,height,ink,paper,atlas,0,W-10,0);return;}
    if(!layout_depth)y=layout_y(y);
    layout_depth++;
    for(;*text&&x+width<=W-10;text++,x+=width){
        unsigned char c=(unsigned char)*text;
        if(c<32||c>126)c='?';
        const unsigned char *g=book_glyph(c);
        if(c==' ')continue;
        for(int j=0;j<height;j++)for(int i=0;i<width-1;i++){
            unsigned alpha;
            if(atlas&&book_font_ok)alpha=book_atlas[c-32][j*16/height][i*12/(width-1)];
            else alpha=g&&(g[j*7/height]&(1u<<(4-i*5/(width-1))))?255:0;
            if(alpha)rect(x+i,y+j,1,1,blend565(paper,ink,alpha));
        }
    }
    layout_depth--;
}
/* 4-bit grayscale, proportional advance, generated at build time. */
static int reader_advance(unsigned char c,int height,int custom,int bold){
    if(c<32||c>126)c='?';
    if(custom&&book_font_ok==1)return (height*3+2)/5;
    if(custom&&book_font_ok==2){
        int value=(int)(book_native_advance[bold?1:0][c-32]*(unsigned)height+book_native_height/2)/book_native_height;
        return value>0?value:1;
    }
    unsigned adv=bold?reader_bold_advance[c-32]:reader_regular_advance[c-32];
    int v=(int)(adv*(unsigned)height+READER_ATLAS_HEIGHT/2)/READER_ATLAS_HEIGHT;
    return v>0?v:1;
}
static int reader_measure(const char *text,int height,int custom,int bold){
    int width=0;
    for(;*text;text++)width+=reader_advance((unsigned char)*text,height,custom,bold);
    return width;
}
static void reader_draw(int x,int y,const char *text,int height,uint16_t ink,uint16_t paper,int custom,int bold,int limit,int overlay){
    if(!layout_depth)y=layout_y(y);
    layout_depth++;
    for(;*text;text++){
        unsigned char c=(unsigned char)*text;
        if(c<32||c>126)c='?';
        int a=reader_advance(c,height,custom,bold);
        if(x+a>limit)break;
        if(c!=' '){
            if(custom&&book_font_ok==1){
                for(int j=0;j<height;j++)for(int i=0;i<a-1;i++){
                    unsigned alpha=book_atlas[c-32][j*16/height][i*12/(a-1)];
                    if(alpha)rect(x+i,y+j,1,1,blend565(paper,ink,alpha));
                }
            }else{
                unsigned sw=READER_ATLAS_WIDTH,sh=READER_ATLAS_HEIGHT;
                const unsigned char *g=bold?reader_bold_pixels[c-32]:reader_regular_pixels[c-32];
                if(custom&&book_font_ok==2){
                    sw=book_native_width;sh=book_native_height;
                    g=book_native_pixels+((bold?95u:0u)+c-32u)*(sw*sh/2);
                }
                int native_height=height*PIXEL_SCALE;
                int width=(sw*(unsigned)native_height+sh-1)/sh;
                for(int j=0;j<native_height;j++)for(int i=0;i<width&&x*PIXEL_SCALE+i<limit*PIXEL_SCALE;i++){
                    unsigned k=(unsigned)j*sh/(unsigned)native_height*sw+(unsigned)i*sh/(unsigned)native_height;
                    if(k>=sw*sh)continue;
                    unsigned alpha=((g[k/2]>>(k%2?0:4))&15u)*17u;
                    int px=x*PIXEL_SCALE+i,py=y*PIXEL_SCALE+j;
                    if(alpha&&px>=0&&px<CW&&py>=0&&py<CH){
                        size_t at=(size_t)py*CW+px;
                        canvas[at]=blend565(overlay?canvas[at]:paper,ink,alpha);
                    }
                }
            }
        }
        x+=a;
    }
    layout_depth--;
}
static void reader_print(int x,int y,const char *text,int height,uint16_t ink,uint16_t paper,int custom,int bold,int limit){
    reader_draw(x,y,text,height,ink,paper,custom,bold,limit,0);
}
static void text_at_limit(int x,int y,const char *s,int scale,uint16_t col,int limit){
#ifdef UI_ATLAS_WIDTH
    if(!layout_depth)y=layout_y(y);
    int style=scale<=1?0:scale==2?1:2,px=x*PIXEL_SCALE,py=y*PIXEL_SCALE;
    for(;*s;s++){
        unsigned char c=(unsigned char)*s;if(c<32||c>126)c='?';
        int advance=ui_advance[style][c-32];if(px+advance>limit*PIXEL_SCALE)break;
#ifdef UI_ATLAS_SPARSE
        unsigned glyph=(unsigned)style*95+c-32,at=ui_offsets[glyph],end=ui_offsets[glyph+1],pos=0;
        while(at<end){
            pos+=ui_data[at++];unsigned count=ui_data[at++];
            for(unsigned k=0;k<count;k++,pos++){
                unsigned value=ui_data[at++];
                for(unsigned half=0;half<2;half++){
                    unsigned pixel=pos*2+half,a=((value>>(half?0:4))&15u)*17u;
                    int xx=px+(int)(pixel%UI_ATLAS_WIDTH),yy=py+(int)(pixel/UI_ATLAS_WIDTH);
                    if(a&&xx>=0&&xx<CW&&yy>=0&&yy<CH){size_t where=(size_t)yy*CW+xx;canvas[where]=blend565(canvas[where],col,a);}
                }
            }
        }
#else
        const unsigned char *g=ui_pixels[style][c-32];
        for(int j=0;j<UI_ATLAS_HEIGHT;j++)for(int i=0;i<UI_ATLAS_WIDTH;i++){
            unsigned k=(unsigned)(j*UI_ATLAS_WIDTH+i),a=((g[k/2]>>(k%2?0:4))&15u)*17u;
            int xx=px+i,yy=py+j;
            if(a&&xx>=0&&xx<CW&&yy>=0&&yy<CH){size_t at=(size_t)yy*CW+xx;canvas[at]=blend565(canvas[at],col,a);}
        }
#endif
        px+=advance;
    }
#else
    int height=scale<=1?12:scale==2?18:26;
    reader_draw(x,y,s,height,col,bg,0,scale>1,limit,1);
#endif
}
static void text_at(int x,int y,const char *s,int scale,uint16_t col){
    text_at_limit(x,y,s,scale,col,W);
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
    rect(0,0,W,26,card);
    text_at(12,7,clock_label,1,fg);
    char b[16]; if(battery>=0) snprintf(b,sizeof(b),"%d%%",battery); else snprintf(b,sizeof(b),"--%%");
    text_at(183,7,b,1,fg);
    rect(220,9,12,8,edge);rect(232,11,2,4,edge);
    if(battery>=0) rect(222,11,battery*8/100,4,mint);
    rect(0,25,W,1,edge);
}
static uint16_t selection_ink(void){return theme==2?bg:RGB(255,255,255);}
static void classic_row(int y,int height,const char *label,int active){
    uint16_t ink=active?selection_ink():fg;
    rect(12,y,216,height,active?mint:bg);
    if(!active)rect(12,y+height-1,216,1,edge);
    text_at_limit(21,y+7,label,2,ink,205);text_at(214,y+7,">",2,active?ink:muted);
}
static void row(int i,const char *label) {
    classic_row(80+i*29,28,label,i==selected);
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
/* Idle writes use exact raw levels and restore only changes we own. */
static int power_raw(const char *dir,long value){
    long max=read_number(dir,"max_brightness");
    if((!allow_backlight||!power_allow)&&!smart_allow)return 0;
    if(max<1||value<0||value>max||(value==0&&!power_blank&&!smart_allow))return 0;
    char v[32];snprintf(v,sizeof(v),"%ld",value);
    return power_attr_write(dir,"brightness",v)&&read_number(dir,"brightness")==value;
}
static int power_wake(void){
    if(!power_stage)return 1;
    long actual=read_number(power_dir,"brightness");
    if(actual<0)return 0;
    if(actual==power_written&&!power_raw(power_dir,power_saved))return 0;
    long max=read_number(power_dir,"max_brightness"),value=read_number(power_dir,"brightness");
    if(max>0&&value>=0&&value<=max)brightness_pct=(int)(value*100/max);
    power_stage=0;power_saved=power_written=-1;power_dir[0]=0;return 1;
}
static void power_idle(int target){
    if((!power_allow||!allow_backlight)&&!(smart_allow&&target==2))return;
    if(power_failed||!target||target<=power_stage)return;
    if(!power_stage){
        if(!backlight_dir[0]||!power_sysfs(backlight_dir)){power_failed=1;return;}
        long value=read_number(backlight_dir,"brightness"),max=read_number(backlight_dir,"max_brightness");
        if(value<=0||max<1||value>max){power_failed=1;return;}
        strcpy(power_dir,backlight_dir);power_saved=value;
    }
    long max=read_number(power_dir,"max_brightness");
    if(max<1){power_failed=1;return;}
    long value=target==2?0:(max+9)/10;if(value>power_saved&&target==1)value=power_saved;
    long current=read_number(power_dir,"brightness");
    if(current<0||(power_stage&&current!=power_written)){power_failed=1;return;}
    if(current!=value&&!power_raw(power_dir,value)){
        /* A driver can reject verification after a successful write. Attempt restoration. */
        (void)power_raw(power_dir,power_saved);power_failed=1;return;
    }
    power_written=value;power_stage=target;
}
static int change_brightness(int pct) {
    if(pct<10)pct=10;
    if(pct>100)pct=100;
    brightness_error=!write_backlight(backlight_dir,pct);
    if(!brightness_error){
        long max=read_number(backlight_dir,"max_brightness"),val=read_number(backlight_dir,"brightness");
        if(max>0&&val>=0&&val<=max)brightness_pct=(int)(val*100/max);
        brightness_saved=pct;save_error=!save_preferences();
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
    if(free_mib>=1024)snprintf(b,sizeof(b),"%d%% USED  %llu.%llu GiB FREE",pct,free_mib/1024,(free_mib%1024)*10/1024);
    else snprintf(b,sizeof(b),"%d%% USED  %llu MiB FREE",pct,free_mib);
    text_at(21,y+23,b,1,muted);
    rect(21,y+37,198,8,edge);rect(21,y+37,198*pct/100,8,mint);
}
static int audio_name(const char *p){return pocketos_demuxer(p)!=NULL;}
static int compare_tracks(const void *a,const void *b){return strcmp(a,b);}
static void scan_folder(const char *relative,int depth,int *budget){
    if(depth>6||music_count>=TRACK_CAP||*budget<=0)return;
    char folder[512];int n=snprintf(folder,sizeof(folder),"%s/%s",music_dir,relative);
    if(n<0||(size_t)n>=sizeof(folder))return;
    DIR *d=opendir(folder);if(!d)return;
    struct dirent *e;
    while(*budget>0&&music_count<TRACK_CAP&&(e=readdir(d))){
        --*budget;
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        char rel[256],path[512];
        n=snprintf(rel,sizeof(rel),"%s%s%s",relative,*relative?"/":"",e->d_name);
        if(n<0||(size_t)n>=sizeof(rel))continue;
        n=snprintf(path,sizeof(path),"%s/%s",music_dir,rel);
        struct stat st;
        if(n<0||(size_t)n>=sizeof(path)||lstat(path,&st)!=0)continue;
        if(S_ISDIR(st.st_mode))scan_folder(rel,depth+1,budget);
        else if(S_ISREG(st.st_mode)&&audio_name(rel))strcpy(music_files[music_count++],rel);
    }
    closedir(d);
}
static void clean_label(char *s){
    for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s>126)*s=' ';
}
static FILE *regular_file(const char *path){
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return NULL;
    struct stat st;
    if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)){close(fd);return NULL;}
    FILE *f=fdopen(fd,"rb");if(!f)close(fd);return f;
}
static int music_cover(const char *path){
    FILE *f=regular_file(path);if(!f)return 0;
    char h[32];const char *magic=fgets(h,sizeof(h),f);
    int ok=magic&&!strcmp(h,"P6\n")&&fgets(h,sizeof(h),f)&&!strcmp(h,"64 64\n")&&
        fgets(h,sizeof(h),f)&&!strcmp(h,"255\n")&&fread(cover,1,sizeof(cover),f)==sizeof(cover)&&fgetc(f)==EOF;
    fclose(f);return ok;
}
static void music_details(void){
    track_title[0]=track_artist[0]=track_album[0]=0;cover_ok=0;
    music_pos=music_length=0;
    if(music_count<1)return;
    const char *base=strrchr(music_files[music_index],'/');
    snprintf(track_title,sizeof(track_title),"%.64s",base?base+1:music_files[music_index]);clean_label(track_title);
    char path[640];
    int n=snprintf(path,sizeof(path),"%s/%s",music_dir,music_files[music_index]);
    if(n<0||(size_t)n>=sizeof(path))return;
    FILE *f=regular_file(path);
    if(f){meta_read(f,track_title,track_artist,track_album,&music_length);fclose(f);}
    n=snprintf(path,sizeof(path),"%s/%s.tags",music_dir,music_files[music_index]);
    if(n<0||(size_t)n>=sizeof(path))return;
    f=regular_file(path);
    if(f){
        char line[160];
        for(int i=0;i<12&&fgets(line,sizeof(line),f);i++){
            line[strcspn(line,"\r\n")]=0;
            if(!strncmp(line,"title=",6))snprintf(track_title,sizeof(track_title),"%.64s",line+6);
            else if(!strncmp(line,"artist=",7))snprintf(track_artist,sizeof(track_artist),"%.64s",line+7);
            else if(!strncmp(line,"album=",6))snprintf(track_album,sizeof(track_album),"%.64s",line+6);
            else if(!strncmp(line,"duration=",9)){
                char *end;long v=strtol(line+9,&end,10);
                if(end!=line+9&&!*end&&v>0&&v<=604800)music_length=(uint32_t)v;
            }
        }
        fclose(f);clean_label(track_title);clean_label(track_artist);clean_label(track_album);
    }
    n=snprintf(path,sizeof(path),"%s/%s.ppm",music_dir,music_files[music_index]);
    if(n<0||(size_t)n>=sizeof(path))return;
    cover_ok=music_cover(path);
    if(!cover_ok){
        const char *slash=strrchr(music_files[music_index],'/');
        size_t length=slash?(size_t)(slash-music_files[music_index])+1:0;
        n=snprintf(path,sizeof(path),"%s/%.*scover.ppm",music_dir,(int)length,music_files[music_index]);
        if(n>0&&(size_t)n<sizeof(path))cover_ok=music_cover(path);
        if(!cover_ok){
            n=snprintf(path,sizeof(path),"%s/%.*sfolder.ppm",music_dir,(int)length,music_files[music_index]);
            if(n>0&&(size_t)n<sizeof(path))cover_ok=music_cover(path);
        }
    }
}
static void filter_playlist(void){
    /* User-created standard relative M3U; match only already-indexed files. */
    char path[512];int n=snprintf(path,sizeof(path),"%s/playlist.m3u",music_dir);
    if(n<0||(size_t)n>=sizeof(path))return;
    FILE *f=regular_file(path);if(!f){music_count=0;return;}
    static char list[TRACK_CAP][256];
    int count=0;char line[512];
    for(int i=0;i<8192&&count<TRACK_CAP&&fgets(line,sizeof(line),f);i++){
        size_t len=strlen(line);
        if(len&&line[len-1]!='\n'&&!feof(f)){int c;while((c=fgetc(f))!=EOF&&c!='\n'){}continue;}
        line[strcspn(line,"\r\n")]=0;
        if(!line[0]||line[0]=='#')continue;
        const char *name=!strncmp(line,"./",2)?line+2:line;
        for(int j=0;j<music_count;j++)if(!strcmp(name,music_files[j])){
            strcpy(list[count++],music_files[j]);break;
        }
    }
    fclose(f);memcpy(music_files,list,(size_t)count*sizeof(list[0]));music_count=count;
}
static void music_scan(void){
    if(music_pid>=0||sd_worker>=0||media_stale)return; /* Never change the playing queue under the decoder. */
    music_count=music_index=music_top=0;
    music_pending=-1;music_start_at=0;music_error=0;
    int budget=8192;scan_folder("",0,&budget);
    qsort(music_files,(size_t)music_count,sizeof(music_files[0]),compare_tracks);
    if(music_list)filter_playlist();
    music_details();
}
static uint32_t library_hash(char files[][256],int count){
    uint32_t hash=2166136261u;
    for(int i=0;i<count;i++){
        for(const unsigned char *p=(const unsigned char*)files[i];*p;p++)hash=(hash^*p)*16777619u;
        hash=(hash^255u)*16777619u;
    }
    return hash;
}
static int music_refresh(void){
    if(music_pid>=0)return 0;
    char name[256]="";if(music_count&&music_index<music_count)strcpy(name,music_files[music_index]);
    int old_count=music_count,top=music_top;
    uint32_t hash=library_hash(music_files,music_count),pos=music_pos,start=music_start_at;
    int err=music_error;
    music_scan();
    for(int i=0;name[0]&&i<music_count;i++)if(!strcmp(name,music_files[i])){
        music_index=i;music_details();music_pos=pos;music_start_at=start;music_error=err;break;
    }
    int max=music_count>4?music_count-4:0;music_top=top>max?max:top;
    return old_count!=music_count||hash!=library_hash(music_files,music_count);
}
static int music_send(unsigned char c){
    if(music_fd<0)return 0;
    return write(music_fd,&c,1)==1;
}
static int music_volume_state(void){
    if(music_pid<0||music_fd<0)return 1; /* Already stopping; use new gain on next start. */
    unsigned char cmd[2]={volume_extended?VOL_ENABLE:VOL_DISABLE,(unsigned char)volume};
    if(music_fd>=0&&write(music_fd,cmd,sizeof(cmd))==(ssize_t)sizeof(cmd))return 1;
    /* If consent/revocation or gain cannot be delivered, stop audio. */
    music_pending=-1;music_switch=music_seek_paused=0;music_manual_stop=1;music_start_at=music_pos;
    if(music_fd>=0){close(music_fd);music_fd=-1;}
    (void)kill(music_pid,SIGTERM);return 0;
}
static int volume_confirm(int jack){
    if(!volume_warning)return 0;
    volume_warning=0;
    if((music_pid>=0&&music_fd<0)||!music_keep_audio(output,jack))return 0;
    volume_extended=1;return music_volume_state();
}
static void volume_revoke(void){
    volume_warning=0;
    if(!volume_extended)return;
    volume_extended=0;volume=vol_clamp(volume,0);(void)music_volume_state();
}
static void music_start(void);
/* Never guess a USB route, or use Bluetooth merely because the radio is on. */
static int audio_auto_route(int jack,int bt){return jack==1?0:bt?2:-1;}
static int audio_route(int preference,int jack){
    if(preference==3)return audio_auto_route(jack,jack==1?0:bt_audio_ready());
    return (preference==2?bt_audio_ready():smart_keep_audio(preference,jack))?preference:-1;
}
static int music_keep_audio(int route,int jack){return audio_route(route,jack)>=0;}
static const char *audio_active_label(void){
    int route=music_pid>=0?music_route:audio_route(output,smart_jack());
    return route>=0&&route<3?outputs[route]:"NO AUDIO DEVICE";
}
static void music_select(int idx){
    if(idx<0||idx>=music_count)return;
    music_index=idx;music_details();
    if(music_index<music_top)music_top=music_index;
    if(music_index>=music_top+4)music_top=music_index-3;
}
static int scroll_top(int top,int count,int visible,int step){
    int max=count>visible?count-visible:0;
    int next=top+step;
    return next<0?0:next>max?max:next;
}
static void music_scroll(int step){music_top=scroll_top(music_top,music_count,4,step);}
static int music_read_report(void){
    if(music_report<0)return 0;
    uint32_t msg[2];int changed=0;ssize_t n;
    while((n=read(music_report,msg,sizeof(msg)))==(ssize_t)sizeof(msg)){
        if(msg[0]<=604800&&msg[0]!=music_pos){music_pos=msg[0];changed=1;}
        if(msg[1]&&msg[1]<=604800&&msg[1]!=music_length){music_length=msg[1];changed=1;}
    }
    return changed;
}
static int music_next_index(void){
    if(music_repeat==2)return music_index;
    if(music_shuffle&&music_count>1){
        music_random=music_random*1664525u+1013904223u;
        return (music_index+1+(int)(music_random%(unsigned)(music_count-1)))%music_count;
    }
    return music_index+1<music_count?music_index+1:music_repeat==1?0:-1;
}
static int music_reap(void){
    if(music_pid<0)return 0;
    int status=0;pid_t n=waitpid(music_pid,&status,WNOHANG);
    if(n==0||(n<0&&errno==EINTR))return 0;
    int was_paused=music_pause,old_route=music_route;
    music_error=n<0||!WIFEXITED(status)?1:WEXITSTATUS(status);
    if(music_fd>=0)close(music_fd);
    if(music_report>=0){(void)music_read_report();close(music_report);}
    music_report=-1;music_fd=-1;music_pid=-1;music_pause=0;music_route=-1;
    int switching=music_switch;music_switch=0;
    int seek_paused=music_seek_paused;music_seek_paused=0;
    if(music_manual_stop){
        music_pos=music_start_at;
        music_pending=-1;
    }else if(music_pending>=0){
        int idx=music_pending;music_pending=-1;
        if(idx>=0&&idx<music_count){music_select(idx);music_pos=music_start_at;music_error=0;if(!seek_paused)music_start();}
    }else if(switching||(output==3&&music_error&&old_route>=0&&audio_route(output,smart_jack())!=old_route)){
        /* Use the last decoder report, never restart a different track. */
        music_start_at=music_pos;music_error=0;
        if(!was_paused&&switching!=2&&music_keep_audio(output,smart_jack()))music_start();
        else if(!music_keep_audio(output,smart_jack()))music_error=12;
    }else if(!music_error&&!was_paused&&music_keep_audio(output,smart_jack())){
        int next=music_next_index();if(next>=0){music_start_at=0;music_select(next);music_start();}
    }else if(was_paused||music_error){music_start_at=music_pos;}
    music_manual_stop=0;output_busy=0;return 1;
}
static void music_start(void){
    if(music_pid>=0)return;
    if(!allow_audio){music_error=2;return;}
    int route=audio_route(output,smart_jack());
    if(route<0){music_error=12;return;}
    if(music_count<1||access("/usr/bin/pocketos-music",X_OK)!=0){music_error=1;return;}
    if(sd_worker>=0||media_stale||!hw_sd_mounted("")){music_error=15;return;}
    char path[512],v[16],seek[16];int n=snprintf(path,sizeof(path),"%s/%s",music_dir,music_files[music_index]);
    if(n<0||(size_t)n>=sizeof(path)){music_error=1;return;}
    snprintf(v,sizeof(v),"%d",vol_clamp(volume,0));
    snprintf(seek,sizeof(seek),"%u",music_start_at);
    int f[2],r[2];if(pipe(f)!=0){music_error=1;return;}
    if(pipe(r)!=0){close(f[0]);close(f[1]);music_error=1;return;}
    int rf=fcntl(r[0],F_GETFL);
    if(rf<0||fcntl(r[0],F_SETFL,rf|O_NONBLOCK)!=0||fcntl(r[0],F_SETFD,FD_CLOEXEC)!=0){close(f[0]);close(f[1]);close(r[0]);close(r[1]);music_error=1;return;}
    int flags=fcntl(f[1],F_GETFL);
    if(flags<0||fcntl(f[1],F_SETFL,flags|O_NONBLOCK)!=0||fcntl(f[1],F_SETFD,FD_CLOEXEC)!=0){close(f[0]);close(f[1]);close(r[0]);close(r[1]);music_error=1;return;}
    pid_t pid=fork();
    if(pid==0){
        close(f[1]);if(dup2(f[0],STDIN_FILENO)<0)_exit(126);close(f[0]);
        close(r[0]);char fdtext[16];snprintf(fdtext,sizeof(fdtext),"%d",r[1]);
        if(route==2)execl("/usr/bin/pocketos-music","pocketos-music","--allow-play",path,v,"--controlled","--start",seek,"--progress-fd",fdtext,"--bluetooth",(char*)NULL);
        else execl("/usr/bin/pocketos-music","pocketos-music","--allow-play",path,v,"--controlled","--start",seek,"--progress-fd",fdtext,(char*)NULL);
        _exit(127);
    }
    close(f[0]);close(r[1]);
    if(pid<0){close(f[1]);close(r[0]);music_error=1;return;}
    music_report=r[0];
    music_pid=pid;music_fd=f[1];music_route=route;music_pause=0;music_error=0;music_manual_stop=0;music_start_at=0;
    if(volume_extended)(void)music_volume_state();
}
/* If pause cannot be queued, stop the child instead of permitting unintended
 * playback; retain last reported position for a later explicit Play. */
static void smart_audio_apply(int jack){
    if(music_keep_audio(output,jack))return;
    volume_revoke();
    if(music_pid<0||music_pause)return;
    music_pending=-1;
    if(music_send(200))music_pause=1;
    else {
        music_manual_stop=1;music_start_at=music_pos;
        if(music_fd>=0){close(music_fd);music_fd=-1;}
        (void)kill(music_pid,SIGTERM);
    }
}
/* Stop/reap the old decoder before opening the new route. No overlapping
 * output streams and no automatic start from an idle or paused state. */
static int music_auto_apply(int desired){
    if(music_pid<0||music_fd<0||music_manual_stop||music_switch||music_pending>=0||desired==music_route)return 0;
    music_switch=music_pause||desired<0?2:1;
    volume_warning=volume_extended=0;volume=vol_clamp(volume,0);
    int sent=music_send(202);close(music_fd);music_fd=-1;
    if(!sent)(void)kill(music_pid,SIGTERM);
    return 1;
}
static int smart_audio_guard(void){
    int jack=smart_jack();
    if(output==3){
        int desired=audio_route(output,jack);
        if(music_auto_apply(desired))return 1;
        if(desired<0&&volume_extended)volume_revoke();
    }else if((music_pid>=0&&!music_pause)||volume_extended)smart_audio_apply(jack);
    return 0;
}
static int smart_audio_test(void){
    int f[2];if(pipe(f))return 10;
    int flags=fcntl(f[0],F_GETFL);if(flags<0||fcntl(f[0],F_SETFL,flags|O_NONBLOCK)<0)return 11;
    music_pid=getpid();music_fd=f[1];output=0;music_pause=0;music_pending=1;
    unsigned char cmd;
    smart_audio_apply(1);if(music_pause||read(f[0],&cmd,1)>=0||errno!=EAGAIN)return 12;
    smart_audio_apply(0);if(!music_pause||music_pending!=-1||read(f[0],&cmd,1)!=1||cmd!=200)return 13;
    smart_audio_apply(1);if(!music_pause||read(f[0],&cmd,1)>=0||errno!=EAGAIN)return 14;
    music_pause=0;smart_audio_apply(-1);if(!music_pause||read(f[0],&cmd,1)!=1||cmd!=200)return 15;
    if(audio_auto_route(1,1)!=0||audio_auto_route(0,1)!=2||audio_auto_route(-1,0)!=-1)return 16;
    output=3;music_route=0;music_pause=0;music_pending=-1;
    if(!music_auto_apply(2)||music_switch!=1||music_fd!=-1||read(f[0],&cmd,1)!=1||cmd!=202)return 17;
    close(f[0]);music_pid=-1;music_switch=0;
    if(pipe(f))return 18;
    music_pid=getpid();music_fd=f[1];music_route=2;music_pause=1;
    if(!music_auto_apply(0)||music_switch!=2||read(f[0],&cmd,1)!=1||cmd!=202)return 19;
    close(f[0]);music_pid=-1;music_route=-1;music_switch=music_pause=0;output=0;return 0;
}
/* Changing tracks never carries a pending seek from the prior track. */
static void music_choose_track(int idx,int play){
    if(idx<0||idx>=music_count)return;
    if(music_pid>=0&&idx==music_index&&music_pending<0)return;
    music_switch=music_seek_paused=0;music_start_at=0;
    if(music_pid>=0){
        music_manual_stop=0;music_pending=idx;
        if(music_fd>=0){(void)music_send(202);close(music_fd);music_fd=-1;}
        return;
    }
    music_pending=-1;music_select(idx);music_error=0;
    if(play)music_start();
}
static void music_action(int action){
    if(action==1){
        if(music_pid<0)music_start();
        else if(music_fd<0)return;
        else if(music_pause&&!music_keep_audio(output,smart_jack()))music_error=12;
        else if(music_send(music_pause?201:200)){music_pause=!music_pause;music_error=0;}
    } else if(action==2){
        music_switch=music_seek_paused=0;volume_revoke();music_pending=-1;music_start_at=0;music_manual_stop=1;
        if(music_pid>=0&&music_fd>=0){(void)music_send(202);close(music_fd);music_fd=-1;}
        else if(music_pid<0){music_pos=0;music_error=0;music_manual_stop=0;}
    } else if(music_count){
        int base=music_pending>=0?music_pending:music_index;
        int idx=(base+(action==0?music_count-1:1))%music_count;
        music_choose_track(idx,0);
    }
}
static const char *music_error_text(int error){
    switch(error){
    case 2:return "PLAYBACK LOCKED";
    case 10:return "AUDIO DECODER MISSING";
    case 11:return "INVALID AUDIO FILE";
    case 12:return output==3?"CONNECT AN AUDIO DEVICE":output==2?"BLUETOOTH NOT CONNECTED":output==1?"USB OUTPUT UNAVAILABLE":"CONNECT HEADPHONES";
    case 13:return "AUDIO ROUTE FAILED";
    case 14:return "ALSA OUTPUT FAILED";
    case 15:return "MICROSD NOT AVAILABLE";
    default:return "PLAYBACK UNAVAILABLE";
    }
}
static void music_seek(int percent){
    if(!music_length||music_count<1||percent<0||percent>100)return;
    uint32_t sec=(uint32_t)((uint64_t)music_length*(unsigned)percent/100);
    if(sec>=music_length)sec=music_length-1;
    music_start_at=sec;music_pos=sec;
    if(music_pid>=0){music_seek_paused=music_pause||music_switch==2;music_switch=0;music_manual_stop=0;music_pending=music_index;if(music_fd>=0){(void)music_send(202);close(music_fd);music_fd=-1;}}
    /* Seeking an idle track sets its next start position without autoplay. */
}
static int book_kind(const char *name){
    const char *e=strrchr(name,'.');if(!e)return 0;
    if(!strcasecmp(e,".txt")||!strcasecmp(e,".text")||!strcasecmp(e,".md")||!strcasecmp(e,".markdown")||!strcasecmp(e,".log"))return 1;
    if(!strcasecmp(e,".acsm")||!strcasecmp(e,".lcpl")||!strcasecmp(e,".kfx"))return 3;
    const char *const ext[]={".epub",".kepub",".pdf",".mobi",".azw",".azw3",".azw4",".fb2",".fbz",".rtf",".docx",".doc",".odt",".html",".htm",".xhtml",".htmlz",".djvu",".djv",".cbz",".cbr",".cb7",".cbc",".chm",".lit",".lrf",".pdb",".prc",".pml",".rb",".snb",".tcr",".txtz",".xml",".tex",".pptx",".xps",".oxps",".ps",".jpg",".jpeg",".png"};
    for(size_t i=0;i<sizeof(ext)/sizeof(ext[0]);i++)if(!strcasecmp(e,ext[i]))return 2;
    return 0;
}
static void scan_books(const char *rel,int depth,int *budget){
    if(depth>6||book_count>=BOOK_CAP||*budget<=0)return;
    char path[512];int n=snprintf(path,sizeof(path),"%s/%s",book_dir,rel);
    if(n<0||(size_t)n>=sizeof(path))return;
    DIR *d=opendir(path);if(!d)return;
    struct dirent *e;
    while(*budget>0&&book_count<BOOK_CAP&&(e=readdir(d))){
        --*budget;
        if(e->d_name[0]=='.')continue;
        if(!depth&&(!strcasecmp(e->d_name,"Fonts")||!strcasecmp(e->d_name,"Dictionaries")||!strcasecmp(e->d_name,"Tools")))continue;
        char child[256];n=snprintf(child,sizeof(child),"%s%s%s",rel,*rel?"/":"",e->d_name);
        if(n<0||(size_t)n>=sizeof(child))continue;
        n=snprintf(path,sizeof(path),"%s/%s",book_dir,child);
        struct stat st;
        if(n<0||(size_t)n>=sizeof(path)||lstat(path,&st)!=0)continue;
        if(S_ISDIR(st.st_mode))scan_books(child,depth+1,budget);
        else if(S_ISREG(st.st_mode)&&book_kind(child)){
            size_t len=strlen(child);
            if(len>=11&&!strcasecmp(child+len-11,".pocket.txt"))continue;
            strcpy(books[book_count++],child);
        }
    }
    closedir(d);
}
static void book_scan(void){
    if(sd_worker>=0||media_stale)return;
    book_count=book_index=book_top=0;book_status[0]=0;
    int budget=4096;scan_books("",0,&budget);
    qsort(books,(size_t)book_count,sizeof(books[0]),compare_tracks);
}
static int book_refresh(void){
    if(book_file||book_prepare_pid>=0)return 0;
    char name[256]="";if(book_count&&book_index<book_count)strcpy(name,books[book_index]);
    int old_count=book_count,top=book_top;
    uint32_t hash=library_hash(books,book_count);
    char note[sizeof(book_status)];memcpy(note,book_status,sizeof(note));
    book_scan();
    for(int i=0;name[0]&&i<book_count;i++)if(!strcmp(name,books[i])){
        book_index=i;memcpy(book_status,note,sizeof(note));break;
    }
    int max=book_count>4?book_count-4:0;book_top=top>max?max:top;
    return old_count!=book_count||hash!=library_hash(books,book_count);
}
static void book_label(int index,char *out,size_t cap){
    if(!cap)return;
    const char *base=strrchr(books[index],'/');base=base?base+1:books[index];
    snprintf(out,cap,"%.*s",(int)cap-1,base);char *ext=strrchr(out,'.');if(ext)*ext=0;clean_label(out);
    char path[600];int n=snprintf(path,sizeof(path),"%s/%s.pocket.meta",book_dir,books[index]);
    if(n<0||(size_t)n>=sizeof(path))return;
    FILE *f=regular_file(path);if(!f)return;
    char magic[40],title[256];
    if(fgets(magic,sizeof(magic),f)&&!strcmp(magic,"POCKETOS-BOOK-1\n")&&fgets(title,sizeof(title),f)){
        title[strcspn(title,"\r\n")]=0;snprintf(out,cap,"%.*s",(int)cap-1,title);clean_label(out);
    }
    fclose(f);
}
static int book_height(void){static const int heights[]={16,19,22,14};return heights[book_size-1];}
static const char *book_size_label(void){static const char *labels[]={"Regular","Large","Extra large","Compact"};return labels[book_size-1];}
static int book_pitch(void){return book_height()+(book_wide?8:4);}
static int book_rows(void){int n=(H-68)/book_pitch();return n<BOOK_ROWS?n:BOOK_ROWS;}
static void book_load_font(void){
    free(book_atlas);book_atlas=NULL;free(book_native_pixels);book_native_pixels=NULL;book_font_ok=0;
    if(!book_font)return;
    char path[512];int n=snprintf(path,sizeof(path),"%s/.fonts/%s.pbf",book_dir,book_font==1?"serif":"dyslexic");
    if(n<0||(size_t)n>=sizeof(path))return;
    FILE *f=regular_file(path);
    if(f){char magic[4]={0};
        if(fread(magic,1,4,f)==4&&!memcmp(magic,"PBF1",4)){
            book_atlas=malloc(95u*sizeof(*book_atlas));
            if(book_atlas&&fread(book_atlas,sizeof(*book_atlas),95,f)==95&&fgetc(f)==EOF)book_font_ok=1;
        }else if(!memcmp(magic,"PBF2",4)){
            unsigned char geometry[4];
            if(fread(geometry,1,4,f)==4){
                unsigned w=geometry[0]|(unsigned)geometry[1]<<8,h=geometry[2]|(unsigned)geometry[3]<<8;
                if(w>=16&&w<=64&&h>=32&&h<=64&&!((w*h)&1)){
                    size_t bytes=2u*95u*w*h/2;book_native_pixels=malloc(bytes);
                    int valid=book_native_pixels&&fread(book_native_advance,1,sizeof(book_native_advance),f)==sizeof(book_native_advance)&&
                        fread(book_native_pixels,1,bytes,f)==bytes&&fgetc(f)==EOF;
                    for(unsigned i=0;valid&&i<2;i++)for(unsigned j=0;j<95;j++)if(!book_native_advance[i][j]||book_native_advance[i][j]>w+8)valid=0;
                    if(valid){book_native_width=w;book_native_height=h;book_font_ok=2;}
                }
            }
        }
        fclose(f);
        if(!book_font_ok){free(book_atlas);book_atlas=NULL;free(book_native_pixels);book_native_pixels=NULL;}
    }
}
static int bookmark_path(char out[600]){
    if(!book_count)return 0;
    int n=snprintf(out,600,"%s/%s.pocket.pos",book_dir,books[book_index]);
    return n>0&&n<600;
}
static void book_resume(void){
    char path[600];if(!bookmark_path(path))return;
    FILE *f=regular_file(path);if(!f)return;
    struct stat st;long long bytes,mtime,pos,prev=-1;
    int size,wide,paper,font,depth=0;char magic[16],extra;
    off_t saved[BOOK_HISTORY];
    if(fstat(fileno(f),&st)||st.st_size>8192||fscanf(f,"%15s",magic)!=1){fclose(f);return;}
    int valid=0;
    if(!strcmp(magic,"POCKETBOOK1")){
        valid=fscanf(f,"%lld %lld %lld %lld %d %d %d %d",&bytes,&mtime,&pos,&prev,&size,&wide,&paper,&font)==8;
        if(valid&&prev>=0&&prev<pos&&prev<=67108864){saved[0]=(off_t)prev;depth=1;}
        else if(valid&&prev!=-1)valid=0;
    }else if(!strcmp(magic,"POCKETBOOK2")){
        valid=fscanf(f,"%lld %lld %lld %d %d %d %d %d",&bytes,&mtime,&pos,&size,&wide,&paper,&font,&depth)==8&&depth>=0&&depth<=BOOK_HISTORY;
        for(int i=0;valid&&i<depth;i++){
            long long offset;
            if(fscanf(f,"%lld",&offset)!=1||offset<0||offset>=pos||offset>67108864||(i&&offset<=(long long)saved[i-1]))valid=0;
            else saved[i]=(off_t)offset;
        }
    }
    if(valid&&fscanf(f," %c",&extra)==EOF&&size>=1&&size<=4&&wide>=0&&wide<=1&&paper>=0&&paper<=3&&font>=0&&font<=2){
        book_size=size;book_wide=wide;book_theme=paper;book_font=font;
        if(bytes==(long long)book_bytes&&mtime==book_mtime&&pos>=0&&pos<bytes){
            book_pos=(off_t)pos;book_depth=depth;
            if(depth)memcpy(book_offsets,saved,(size_t)depth*sizeof(saved[0]));
        }
    }
    fclose(f);
}
static void book_save(void){
    if(!book_file||!book_dirty||!book_allow_save)return;
    char path[600],tmp[648];if(!bookmark_path(path))return;
    struct stat st;
    if(lstat(path,&st)==0&&!S_ISREG(st.st_mode)){book_save_error=1;return;}
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",path);
    if(n<0||(size_t)n>=sizeof(tmp)){book_save_error=1;return;}
    int fd=mkstemp(tmp);if(fd<0){book_save_error=1;return;}
    FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);book_save_error=1;return;}
    int ok=fprintf(f,"POCKETBOOK2 %lld %lld %lld %d %d %d %d %d\n",(long long)book_bytes,book_mtime,(long long)book_pos,book_size,book_wide,book_theme,book_font,book_depth)>0;
    for(int i=0;ok&&i<book_depth;i++)if(fprintf(f,"%lld\n",(long long)book_offsets[i])<0)ok=0;
    if(fflush(f)!=0||fsync(fd)!=0)ok=0;
    if(fclose(f)!=0)ok=0;
    if(ok&&rename(tmp,path)==0){book_dirty=0;book_save_error=0;}else{unlink(tmp);book_save_error=1;}
}
static int book_char(FILE *f){
    int c=fgetc(f);
    if(c==EOF||c=='\n'||c=='\f')return c;
    if(c=='\r'){int a=fgetc(f);if(a!='\n'&&a!=EOF)ungetc(a,f);return '\n';}
    if(c=='\t')return ' ';
    if(c>=32&&c<127)return c;
    /* Consume continuation bytes as a single replacement glyph. */
    if(c>=194&&c<=244){
        int count=c<224?1:c<240?2:3;
        unsigned cp=(unsigned)c&(count==1?31u:count==2?15u:7u);
        for(int j=0;j<count;j++){int a=fgetc(f);if(a==EOF)return '?';if((a&192)!=128){ungetc(a,f);return '?';}cp=(cp<<6)|((unsigned)a&63);}
        if(cp==0xa0)return ' ';
        if(cp>=0x2010&&cp<=0x2015)return '-';
        if(cp==0x2018||cp==0x2019||cp==0x201a||cp==0x2032)return '\'';
        if(cp==0x201c||cp==0x201d||cp==0x201e||cp==0x2033)return '"';
        if(cp==0x2026)return '.';
        if(cp>=0xc0&&cp<=0xff){
            static const char latin[]="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
            return latin[cp-0xc0];
        }
    }
    return '?';
}
static void book_page(void){
    memset(book_text,0,sizeof(book_text));memset(book_indent,0,sizeof(book_indent));
    memset(book_bold,0,sizeof(book_bold));book_eof=0;book_render_rows=0;
    if(!book_file||fseeko(book_file,book_pos,SEEK_SET)!=0){book_eof=1;return;}
    int paragraph=book_pos==0,heading=0,y=34,rows=book_rows(),page_end=0;
    if(book_pos>0&&fseeko(book_file,book_pos-1,SEEK_SET)==0){int prev=fgetc(book_file);paragraph=prev=='\n'||prev=='\f';}
    for(int r=0;r<rows&&y+book_height()<H-32;r++){
        if(book_reflow&&paragraph){
            for(;;){
                off_t at=ftello(book_file);int c=fgetc(book_file);
                if(c=='\n'||c=='\r')continue;
                if(c=='\f'){
                    if(r){page_end=1;break;}
                    continue;
                }
                if(c==EOF){book_eof=1;break;}
                if(c=='#'){
                    int next=fgetc(book_file);
                    if(next==' '){heading=1;break;}
                }
                if(fseeko(book_file,at,SEEK_SET)!=0)book_eof=1;
                break;
            }
            if(page_end||book_eof)break;
        }
        book_indent[r]=book_reflow&&paragraph&&!heading?12:0;book_bold[r]=heading;book_y[r]=y;
        int n=0,c=0,last_space=-1,width=book_indent[r],wrapped=0;
        off_t space_end=-1;
        while(n<64){
            off_t before=ftello(book_file);c=book_char(book_file);
            if(c==EOF||c=='\n'||c=='\f')break;
            int advance=reader_advance((unsigned char)c,book_height(),book_font!=0,heading);
            if(width+advance>W-32){
                wrapped=1;
                if(c==' ')break;
                if(last_space>0&&space_end>=0){n=last_space;if(fseeko(book_file,space_end,SEEK_SET)!=0)book_eof=1;}
                else if(fseeko(book_file,before,SEEK_SET)!=0)book_eof=1;
                break;
            }
            if(c==' '){last_space=n;space_end=ftello(book_file);}
            book_text[r][n++]=(char)c;width+=advance;
        }
        while(n>0&&book_text[r][n-1]==' ')n--;
        book_text[r][n]=0;book_render_rows=r+1;y+=book_pitch()+(heading?4:0);
        if(c==EOF||book_eof){book_eof=1;break;}
        if(c=='\f'){page_end=1;break;}
        if(!wrapped&&c=='\n'){paragraph=1;heading=0;}else paragraph=0;
    }
    book_next=ftello(book_file);
    if(book_next<0||book_next>=book_bytes||book_next<=book_pos)book_eof=1;
    if(!book_page_span&&book_next>book_pos)book_page_span=book_next-book_pos;
}
static void book_close(void){if(book_file){book_save();fclose(book_file);}book_file=NULL;book_depth=0;book_menu=0;free(book_atlas);book_atlas=NULL;free(book_native_pixels);book_native_pixels=NULL;book_font_ok=0;}
static int book_prepare_start(const char *path){
    if(!book_allow_prepare||access("/usr/bin/pocketos-book-prepare",X_OK)!=0){strcpy(book_status,"NATIVE EPUB PREPARER UNAVAILABLE");return 0;}
    if(book_prepare_pid>0){strcpy(book_status,"A BOOK IS ALREADY BEING PREPARED");return 0;}
    pid_t child=fork();
    if(child==0){
        int log=open("/run/pocketos-book-prepare.log",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
        if(log>=0){(void)dup2(log,STDOUT_FILENO);(void)dup2(log,STDERR_FILENO);close(log);}
        execl("/usr/bin/pocketos-book-prepare","pocketos-book-prepare","--epub",path,(char*)NULL);_exit(127);
    }
    if(child<0){strcpy(book_status,"BOOK PREPARATION FAILED");return 0;}
    book_prepare_pid=child;snprintf(book_prepare_name,sizeof(book_prepare_name),"%s",books[book_index]);
    strcpy(book_status,"PREPARING EPUB...");return 0;
}
static int book_open(void){
    book_close();
    if(!book_count)return 0;
    if(!strcmp(book_dir,"/mnt/sd_0/Books")&&(sd_worker>=0||media_stale||!hw_sd_mounted(""))){strcpy(book_status,"MICROSD NOT AVAILABLE");return 0;}
    int kind=book_kind(books[book_index]);char path[600];
    if(kind==3){strcpy(book_status,"DRM PROVIDER NOT INSTALLED");return 0;}
    int n=snprintf(path,sizeof(path),"%s/%s%s",book_dir,books[book_index],kind==2?".pocket.txt":"");
    if(n<0||(size_t)n>=sizeof(path))return 0;
    book_file=regular_file(path);
    if(!book_file){
        const char *ext=strrchr(books[book_index],'.');
        if(kind==2&&ext&&(!strcasecmp(ext,".epub")||!strcasecmp(ext,".kepub"))){
            n=snprintf(path,sizeof(path),"%s/%s",book_dir,books[book_index]);
            if(n>0&&(size_t)n<sizeof(path))return book_prepare_start(path);
        }
        strcpy(book_status,kind==2?"FORMAT NEEDS PC CONVERSION":"CANNOT OPEN BOOK");return 0;
    }
    struct stat st;
    if(fstat(fileno(book_file),&st)!=0||st.st_size<=0||st.st_size>67108864){
        book_close();strcpy(book_status,"EMPTY OR TOO LARGE");return 0;
    }
    book_page_span=0;book_bytes=st.st_size;book_mtime=(long long)st.st_mtime;book_pos=0;book_depth=0;book_status[0]=0;
    book_dirty=0;book_save_error=0;book_menu=0;
    const char *name=strrchr(books[book_index],'/');snprintf(book_title,sizeof(book_title),"%.64s",name?name+1:books[book_index]);
    char *ext=strrchr(book_title,'.');if(ext)*ext=0;
    book_reflow=kind==2;
    char meta_path[600];n=snprintf(meta_path,sizeof(meta_path),"%s/%s.pocket.meta",book_dir,books[book_index]);
    if(n>0&&(size_t)n<sizeof(meta_path)){
        FILE *meta=regular_file(meta_path);
        if(meta){
            char magic[40],title[256];
            if(fgets(magic,sizeof(magic),meta)&&!strcmp(magic,"POCKETOS-BOOK-1\n")&&fgets(title,sizeof(title),meta)){
                title[strcspn(title,"\r\n")]=0;snprintf(book_title,sizeof(book_title),"%.64s",title);clean_label(book_title);
            }
            fclose(meta);
        }
    }
    book_resume();book_load_font();book_page();page=READER;return 1;
}
static int book_prepare_reap(void){
    if(book_prepare_pid<0)return 0;
    int status=0;pid_t got=waitpid(book_prepare_pid,&status,WNOHANG);
    if(!got||(got<0&&errno==EINTR))return 0;
    book_prepare_pid=-1;
    int current=book_count&&book_index<book_count&&!strcmp(books[book_index],book_prepare_name);
    if(got>0&&WIFEXITED(status)&&!WEXITSTATUS(status)){
        if(current&&page==BOOKS){book_status[0]=0;(void)book_open();}
    }else if(current)strcpy(book_status,"CANNOT PREPARE / CHECK BOOK");
    return 1;
}
static void book_turn(int forward){
    if(!book_file)return;
    if(forward&&!book_eof){
        if(book_depth==BOOK_HISTORY){memmove(book_offsets,book_offsets+1,(BOOK_HISTORY-1)*sizeof(book_offsets[0]));book_depth--;}
        book_offsets[book_depth++]=book_pos;book_pos=book_next;book_page();book_dirty++;if(book_dirty>=12)book_save();
    }else if(!forward&&book_depth){book_pos=book_offsets[--book_depth];book_page();book_dirty++;if(book_dirty>=12)book_save();}
}
static void book_select(int direction){
    if(!book_count)return;
    int next=book_index+direction;
    book_index=next<0?0:next>=book_count?book_count-1:next;
    if(book_index<book_top)book_top=book_index;
    if(book_index>=book_top+4)book_top=book_index-3;
    book_status[0]=0;
}
static void book_scroll(int step){book_top=scroll_top(book_top,book_count,4,step);}
static void fm_scroll(int step){
    fm_top=scroll_top(fm_top,fm_count,5,step);
    if(fm_index<fm_top)fm_index=fm_top;
    if(fm_count&&fm_index>=fm_top+5)fm_index=fm_top+4;
}
static void fm_note(const char *text){snprintf(fm_message,sizeof(fm_message),"%.39s",text);}
static void fm_start(void){
    fm_panel=fm_confirm=fm_edit_new=fm_action=0;fm_message[0]=0;
    int pct;unsigned long long space;
    if(sd_worker>=0||media_stale||!hw_sd_mounted("")||!mounted_usage("/mnt/sd_0",&pct,&space)||!fm_attach("/mnt/sd_0")||!fm_scan()){
        fm_close();fm_note("MICROSD NOT AVAILABLE");
    }
}
/* Detect loss of the mounted card before using retained handles. No automatic
 * format, forced unmount or mount during the updater's recovery handoff. */
static int media_probe(void){
    struct stat st;
    int online=!media_stale&&hw_sd_mounted("")&&stat("/mnt/sd_0",&st)==0;
    int changed=online!=media_online||(online&&(media_dev!=st.st_dev||media_ino!=st.st_ino));
    if(!changed)return 0;
    int was_online=media_online;
    media_online=online;
    if(online){media_dev=st.st_dev;media_ino=st.st_ino;}
    if(was_online==1){
        if(!online)media_stale=1;
        /* The old card may have disappeared. Do not save through stale files. */
        if(book_file){fclose(book_file);book_file=NULL;book_close();if(page==READER)page=BOOKS;}
        if(book_prepare_pid>0)(void)kill(book_prepare_pid,SIGTERM);
        if(music_pid>=0){music_pending=-1;music_switch=music_seek_paused=0;music_manual_stop=1;music_start_at=0;
            if(music_fd>=0){close(music_fd);music_fd=-1;}(void)kill(music_pid,SIGTERM);}
        music_count=music_top=music_index=0;music_details();book_count=book_top=book_index=0;
        fm_close();fm_panel=fm_confirm=0;
    }
    if(online){
        /* No SD library traversal on the initial HOME boot. Scan on entry. */
        if((page==MUSIC||page==NOWPLAYING)&&music_pid<0)music_scan();
        if(page==BOOKS&&book_prepare_pid<0)book_scan();
        if(page==FILES)fm_start();
    }else{
        music_count=music_index=music_top=0;music_details();
        book_count=book_index=book_top=0;strcpy(book_status,"MICROSD NOT AVAILABLE");
        fm_close();fm_note("MICROSD NOT AVAILABLE");
    }
    return 1;
}
static void sd_refresh_start(void){
    if(sd_worker>=0||media_online!=0||update_pid>=0||fm_worker>=0||music_pid>=0||
       book_file||book_prepare_pid>=0||page==UPDATES||radio_pid>=0)return;
    if(access("/usr/bin/pocketos-sd",X_OK)!=0)return;
    if(!media_stale&&access("/sys/class/block/mmcblk0p1/partition",R_OK)!=0)return;
    fm_close();
    pid_t pid=fork();
    if(pid==0){
        int log=open("/run/pocketos-sd.log",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
        if(log>=0){(void)dup2(log,STDERR_FILENO);(void)dup2(log,STDOUT_FILENO);close(log);}
        execl("/usr/bin/pocketos-sd","pocketos-sd",media_stale?"replace-stale":"refresh",(char*)NULL);
        _exit(127);
    }
    if(pid>0)sd_worker=pid;
}
static int sd_refresh_poll(void){
    if(sd_worker<0)return 0;
    int status;pid_t p=waitpid(sd_worker,&status,WNOHANG);
    if(p==0||(p<0&&errno==EINTR))return 0;
    sd_worker=-1;
    if(p>0&&WIFEXITED(status)&&WEXITSTATUS(status)==0){media_stale=0;return media_probe();}
    return 0;
}
static void fm_open_media(void){
    if(fm_count<1)return;
    struct fm_item *i=&fm_items[fm_index];
    if(i->dir){if(!fm_enter(i->name))fm_note("CANNOT OPEN FOLDER");else fm_message[0]=0;return;}
    char rel[FM_PATH+256];int n=snprintf(rel,sizeof(rel),"%s%s%s",fm_dir,*fm_dir?"/":"",i->name);
    if(n<0||(size_t)n>=sizeof(rel))return;
    if(!strncmp(rel,"Music/",6)&&audio_name(rel)){
        if(music_pid>=0){fm_note("STOP ACTIVE MUSIC FIRST");return;}
        music_list=0;music_scan();
        for(int j=0;j<music_count;j++)if(!strcmp(music_files[j],rel+6)){fm_close();music_select(j);page=NOWPLAYING;return;}
    }else if(!strncmp(rel,"Books/",6)&&book_kind(rel)){
        book_scan();
        for(int j=0;j<book_count;j++)if(!strcmp(books[j],rel+6)){
            book_index=j;book_top=j>=4?j-3:0;
            fm_close();if(!book_open())page=BOOKS;
            return;
        }
    }
    fm_note("NO APP / MOVE TO MUSIC OR BOOKS");
}
static int fm_writable(void){
    if(!fm_allow||fm_root<0)return 0;
    FILE *f=fopen("/proc/mounts","r");if(!f)return 0;
    char line[1024],dev[128],loc[128],type[32],opts[512];int ok=0;
    while(fgets(line,sizeof(line),f)){
        if(sscanf(line,"%127s %127s %31s %511s",dev,loc,type,opts)!=4)continue;
        if(strcmp(dev,"/dev/mmcblk0p1")||strcmp(loc,"/mnt/sd_0")||strcmp(type,"vfat"))continue;
        int bits=0;char *save=NULL;
        for(char *o=strtok_r(opts,",",&save);o;o=strtok_r(NULL,",",&save)){
            if(!strcmp(o,"rw"))bits|=1;else if(!strcmp(o,"nosuid"))bits|=2;
            else if(!strcmp(o,"nodev"))bits|=4;else if(!strcmp(o,"noexec"))bits|=8;
        }
        if(bits==15)ok=1;
    }
    fclose(f);
    struct stat a,b;
    return ok&&!fstat(fm_root,&a)&&!stat("/mnt/sd_0",&b)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;
}
static void fm_cancel(void){
    if(fm_confirm==2||fm_confirm==3||(fm_panel==2&&!fm_edit_new))fm_clip=0;
    fm_panel=fm_confirm=fm_edit_new=0;fm_note("CANCELLED");
}
static void fm_operation(int action){
    fm_panel=0;
    if(!fm_writable()){fm_note("FILE WRITES LOCKED / READ ONLY");return;}
    if(music_pid>=0||book_file){fm_note("CLOSE ACTIVE MEDIA FIRST");return;}
    if(action==5){
        if(*fm_dir&&!fm_user_dir(fm_dir)){fm_note("PROTECTED LOCATION");return;}
        fm_edit_new=1;fm_rename[0]=0;fm_panel=2;return;
    }
    if(action==2){
        if(!fm_clip){fm_note("NOTHING TO PASTE");return;}
        if(!fm_user_file(fm_dir,fm_source_name)){fm_note("OPEN A USER FOLDER");return;}
        fm_confirm=1;return;
    }
    if(!fm_count||fm_items[fm_index].dir){fm_note("SELECT A REGULAR FILE");return;}
    const char *name=fm_items[fm_index].name;
    if(!fm_capture(name,action==0?1:2)){fm_note("PROTECTED LOCATION OR FILE");return;}
    if(action==0||action==1)fm_note(action==0?"COPIED TO CLIPBOARD":"MOVE: CHOOSE DESTINATION");
    else if(action==3){fm_confirm=2;}
    else if(action==4){
        if(strlen(name)>=sizeof(fm_rename)){fm_clip=0;fm_note("NAME TOO LONG TO EDIT");return;}
        fm_edit_new=0;strcpy(fm_rename,name);fm_panel=2;
    }
}
static int fm_begin_transfer(int action){
    int pipefd[2];if(fm_worker>=0||pipe(pipefd)!=0)return 0;
    int flags=fcntl(pipefd[0],F_GETFL),outflags=fcntl(pipefd[1],F_GETFL);
    if(flags<0||outflags<0||fcntl(pipefd[0],F_SETFL,flags|O_NONBLOCK)!=0||
       fcntl(pipefd[1],F_SETFL,outflags|O_NONBLOCK)!=0||
       fcntl(pipefd[0],F_SETFD,FD_CLOEXEC)!=0||fcntl(pipefd[1],F_SETFD,FD_CLOEXEC)!=0){
        close(pipefd[0]);close(pipefd[1]);return 0;
    }
    pid_t child=fork();
    if(child==0){
        close(pipefd[0]);fm_progress_fd=pipefd[1];fm_cancel_transfer=0;
        struct sigaction sa={.sa_handler=fm_transfer_stop};sigemptyset(&sa.sa_mask);
        (void)sigaction(SIGTERM,&sa,NULL);(void)sigaction(SIGINT,&sa,NULL);
        int code=fm_transfer(action==3?fm_rename:fm_source_name,action==3||fm_clip==2);
        close(pipefd[1]);_exit(code==1?0:code==2?2:1);
    }
    close(pipefd[1]);if(child<0){close(pipefd[0]);return 0;}
    fm_worker=child;fm_worker_fd=pipefd[0];fm_worker_action=action;fm_percent=0;
    fm_note(action==3?"RENAMING":fm_clip==2?"MOVING":"COPYING");
    return 1;
}
static int fm_poll_transfer(void){
    if(fm_worker<0)return 0;
    unsigned pct;int changed=0;
    for(int i=0;i<128&&read(fm_worker_fd,&pct,sizeof(pct))==(ssize_t)sizeof(pct);i++){
        if(pct<=100&&pct>fm_percent){fm_percent=pct;changed=1;}
    }
    int status=0;pid_t got=waitpid(fm_worker,&status,WNOHANG);
    if(got==0||(got<0&&errno==EINTR))return changed;
    close(fm_worker_fd);fm_worker_fd=-1;fm_worker=-1;
    int code=got>0&&WIFEXITED(status)?WEXITSTATUS(status):1;
    fm_note(code==0?"DONE":code==2?"COPIED; SOURCE NOT REMOVED":"FAILED / EXISTS / NO SPACE");
    if(code!=1||fm_worker_action==3)fm_clip=0;
    (void)fm_scan();return 1;
}
static void fm_apply(void){
    int action=fm_confirm;fm_confirm=0;
    if(!fm_writable()||music_pid>=0||book_file){fm_clip=0;fm_note("FILE WRITES LOCKED");return;}
    int result=0;
    if(action==1||action==3){
        fm_edit_new=0;
        if(!fm_begin_transfer(action))fm_note("COULD NOT START TRANSFER");
        return;
    }
    if(action==2)result=fm_delete();
    else if(action==4)result=fm_mkdir(fm_rename);
    fm_edit_new=0;
    fm_note(result==2?"COPIED; SOURCE NOT REMOVED":result?"DONE":"FAILED / EXISTS / NO SPACE");
    if(action==2||action==3)fm_clip=0;
    (void)fm_scan();
}
static void fm_edit_char(int x,int y){
    const char *keys="abcdefghijklmnopqrstuvwxyz0123456789.-_ ";
    if(y>=104&&y<244&&x>=10&&x<230){
        int idx=(y-104)/28*8+(x-10)/28;
        if(idx>=0&&idx<40){
            size_t n=strlen(fm_rename);
            if(n+1<sizeof(fm_rename)){fm_rename[n]=keys[idx];fm_rename[n+1]=0;}
        }
    }else if(y>=251&&y<280){
        if(x<85){size_t n=strlen(fm_rename);if(n)fm_rename[n-1]=0;}
        else if(x<151)fm_rename[0]=0;
        else if(fm_new_name_ok(fm_rename)){fm_panel=0;fm_confirm=fm_edit_new?4:3;}
    }else if(y>=288){fm_cancel();}
}
static const char *radio_keys(void){
    static const char *const layers[]={
        "abcdefghijklmnopqrstuvwxyz0123456789.-_ ",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_ ",
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_\x60{|}~ 0123456"
    };
    return layers[radio_key_layer];
}
static void radio_open(int bluetooth){
    if(radio_pid>=0)return;
    radio_clear_key();radio_mode=bluetooth;radio_count=radio_top=radio_index=0;
    page=bluetooth?BTUI:WIFIUI;radio_message(radio_allow?"Tap Scan to discover":"Radio changes locked");
}
static void radio_choose(void){
    if(!radio_count)return;
    if(radio_pid>=0){if(radio_active==R_BT_SCAN)radio_cancel_scan();else return;}
    if(radio_mode){(void)radio_spawn(R_BT_PAIR,radio_items[radio_index].address);return;}
    struct radio_item *item=&radio_items[radio_index];
    if(!radio_allow){radio_message("Radio changes locked");return;}
    if(item->security==2){radio_message("Security type not supported yet");return;}
    snprintf(radio_ssid,sizeof(radio_ssid),"%.32s",item->name);
    radio_security=item->security;radio_clear_key();radio_keyboard=1;
    radio_message(radio_security?"Enter Wi-Fi password":"Open network - confirm Join");
}
static void radio_disconnect_audio(void){
    if(radio_active==R_BT_SCAN)radio_cancel_scan();
    if(radio_pid>=0)return;
    char selected[18];
    if(bt_audio_selected(selected))(void)radio_spawn(R_BT_DISCONNECT,selected);
    else if(radio_count)(void)radio_spawn(R_BT_DISCONNECT,radio_items[radio_index].address);
}
static void radio_tap(int x,int y){
    if(radio_keyboard){
        if(radio_security&&x>=180&&y>=55&&y<79){radio_show_key=!radio_show_key;return;}
        if(y>=99&&y<239&&x>=8&&x<232&&radio_security){
            int i=(y-99)/28*8+(x-8)/28;
            const char *keys=radio_keys();
            if(i>=0&&(size_t)i<strlen(keys)){
                size_t n=strlen(radio_key);
                if(n<64){radio_key[n]=keys[i];radio_key[n+1]=0;}
            }
        }else if(y>=245&&y<277){
            if(x<74)radio_key_layer=(radio_key_layer+1)%3;
            else if(x<148){size_t n=strlen(radio_key);if(n)radio_key[n-1]=0;}
            else{
                size_t n=strlen(radio_key);
                if(radio_security&&n<8){radio_message("Password must have 8+ characters");return;}
                if(radio_security&&n==64){for(size_t i=0;i<n;i++)if(!isxdigit((unsigned char)radio_key[i])){radio_message("64 characters must be hexadecimal");return;}}
                if(radio_spawn(R_WIFI_JOIN,NULL))radio_clear_key();
            }
        }else if(y>=288)radio_clear_key();
        return;
    }
    if(radio_pid>=0&&radio_active!=R_BT_SCAN)return;
    if(!radio_mode){
        if(x>=12&&x<228&&y>=83&&y<219&&(y-83)%34<31){
            int i=radio_top+(y-83)/34;
            if(i<radio_count){radio_index=i;radio_choose();}
        }else if(y>=224&&y<250&&x>=12&&x<228){
            if(x<116)(void)radio_spawn(R_WIFI_SCAN,NULL);
            else if(x>=124)(void)radio_spawn(R_WIFI_RECONNECT,NULL);
        }else if(y>=254&&y<282&&x>=12&&x<228){
            if(x<116)(void)radio_spawn(R_WIFI_STATUS,NULL);
            else if(x>=124)(void)radio_spawn(R_WIFI_DISCONNECT,NULL);
        }
        return;
    }
    if(y>=83&&y<219){
        int i=radio_top+(y-83)/34;
        if(i<radio_count){radio_index=i;radio_message("Select Pair or Connect");}
    }else if(y>=224&&y<250){
        if(x<82)radio_select(-1);
        else if(x<156)radio_choose();
        else radio_select(1);
    }else if(y>=254&&y<282){
        if(x<82)(void)radio_spawn(radio_mode?R_BT_SCAN:R_WIFI_SCAN,NULL);
        else if(x<156)(void)radio_spawn(radio_mode?R_BT_STATUS:R_WIFI_STATUS,NULL);
        else if(radio_mode){if(radio_count)(void)radio_spawn(R_BT_CONNECT,radio_items[radio_index].address);}
        else (void)radio_spawn(R_WIFI_DISCONNECT,NULL);
    }
}
static void quick_tile(int i,const char *label,const char *value,int active) {
    int y=82+i*31;
    classic_row(y,30,label,i==selected);
    text_at_limit(143,y+9,value,1,i==selected?selection_ink():active?mint:muted,208);
}
static void read_build_id(char out[17]){
    strcpy(out,"unknown");
    FILE *f=fopen("/etc/pocketos-release","r");if(!f)return;
    char line[128];
    while(fgets(line,sizeof(line),f)){
        if(strncmp(line,"BUILD_ID=",9))continue;
        size_t n=strcspn(line+9,"\r\n");
        if(n==16&&strspn(line+9,"0123456789abcdef")==16){memcpy(out,line+9,16);out[16]=0;}
        break;
    }
    fclose(f);
}
static void list_scrollbar(int y,int height,int top,int count,int visible){
    if(count<=visible)return;
    int thumb=height*visible/count;if(thumb<10)thumb=10;
    int offset=(height-thumb)*top/(count-visible);
    if(offset<0)offset=0;
    if(offset>height-thumb)offset=height-thumb;
    rect(232,y,3,height,edge);rect(232,y+offset,3,thumb,mint);
}
static const char *page_label(enum page p){
    switch(p){
    case HOME:return "Home";case MUSIC:return "Music";case BOOKS:return "Books";
    case FILES:return "Files";case SETTINGS:return "Settings";case DIAGNOSTICS:return "Device info";
    case DISPLAY:return "Screen";case SOUND:return "Sound";case RADIO:return "Connections";
    case POWER:return "Battery";case STORAGE:return "Storage";case UPDATES:return "Updates";
    case PREFERENCES:return "Appearance";case CONTROL:return "Quick controls";
    case NOWPLAYING:return "Now playing";case WIFIUI:return "Wi-Fi";case BTUI:return "Bluetooth";
    default:return titles[p];
    }
}
static int navigation_available(void){
    if(page==HOME||page==READER||page==CONTROL)return 0;
    if((page==WIFIUI||page==BTUI)&&radio_keyboard)return 0;
    if(update_pid>=0||update_handoff||radio_pid>=0||volume_warning)return 0;
    if(page==UPDATES&&update_confirm)return 0;
    if(page==FILES&&(fm_worker>=0||fm_confirm||fm_panel))return 0;
    return 1;
}
static void navigation_bar(void){
    if(!navigation_available())return;
    rect(0,293,W,27,card);rect(0,293,W,1,edge);
    if(page==MUSIC||page==BTUI){
        rect(81,298,1,16,edge);rect(155,298,1,16,edge);
        text_at(20,298,"Back",2,fg);text_at(169,298,"Home",2,mint);
        if(page==MUSIC)text_at(89,299,"Playing",1,mint);
        else reader_print(87,300,"Disconnect",10,mint,card,0,0,151);
    }else{
        rect(120,298,1,16,edge);
        text_at(24,298,"< Back",2,fg);text_at(153,298,"Home",2,mint);
    }
}
static int ui_text_width(const char *text,int scale){
#ifdef UI_ATLAS_WIDTH
    unsigned width=0;int style=scale<=1?0:scale==2?1:2;
    for(;*text;text++){unsigned c=(unsigned char)*text;if(c<32||c>126)c='?';width+=ui_advance[style][c-32];}
    return (int)(width+PIXEL_SCALE-1)/PIXEL_SCALE;
#else
    return reader_measure(text,scale<=1?12:scale==2?18:26,0,scale>1);
#endif
}
static void centered_label(int y,const char *text,int scale,uint16_t ink){
    int x=(W-ui_text_width(text,scale))/2;
    text_at_limit(x<12?12:x,y,text,scale,ink,228);
}
static void record_placeholder(int x,int y){
    rect(x,y,128,128,card);
    for(int j=-49;j<=49;j++)for(int i=-49;i<=49;i++){
        int r=i*i+j*j;
        if(r<=49*49)rect(x+64+i,y+64+j,1,1,r<=9*9?bg:r<=21*21?mint:RGB(39,48,63));
    }
}
static void render(void) {
    palette();
    rect(0,0,W,H,bg);statusbar();
    centered_label(37,page==HOME?"PocketOS":page_label(page),2,fg);
    rect(12,59,216,1,edge);
    if(page==CONTROL) {
        char v[16];
        text_at(12,64,"Quick settings",1,muted);
        quick_tile(0,"Wi-Fi",radio_wifi_state()==2?"Connected":radio_wifi_state()==1?"Joining":"Offline",radio_wifi_state()==2);
        quick_tile(1,"Bluetooth",bt_present?"Available":"Off",bt_present);
        if(brightness_pct>=0)snprintf(v,sizeof(v),"%d%%",brightness_pct);
        else snprintf(v,sizeof(v),"UNAVAILABLE");
        quick_tile(2,"Brightness",v,brightness_pct>=0);
        snprintf(v,sizeof(v),"%d%%",volume);quick_tile(3,"Volume",v,0);
        quick_tile(4,"Battery saver",saver?"On":"Off",saver);
        quick_tile(5,"Settings","Open",1);

        text_at(12,301,"< Back",2,mint);
    } else if(page==FILES){
        if(fm_worker>=0){
            reader_print(12,95,fm_message,18,fg,bg,0,1,228);
            reader_print(12,125,fm_source_name,13,muted,bg,0,0,228);
            rect(12,164,216,10,edge);rect(12,164,(int)(216*fm_percent/100),10,mint);
            char label[16];snprintf(label,sizeof(label),"%u%%",fm_percent);
            reader_print(12,181,label,18,mint,bg,0,1,228);
            reader_print(12,221,"Keep microSD inserted",13,fg,bg,0,0,228);
            reader_print(12,245,"Verifying moves before removal",11,muted,bg,0,0,228);
        }else if(fm_confirm){
            reader_print(14,65,"Confirm file operation",17,fg,bg,0,1,228);
            reader_print(14,103,fm_confirm==4?"Create folder?":fm_confirm==2?"Delete permanently?":fm_confirm==3?"Rename file?":fm_clip==2?"Move file here?":"Copy file here?",16,fg,bg,0,0,228);
            reader_print(14,144,fm_confirm==4?fm_rename:fm_source_name,14,muted,bg,0,0,228);
            if(fm_confirm==3)reader_print(14,168,fm_rename,14,mint,bg,0,0,228);
            rounded(12,246,102,35,card);rounded(126,246,102,35,mint);
            text_at(23,259,"CANCEL",1,fg);text_at(139,259,"CONFIRM",1,bg);
        }else if(fm_panel==2){
            reader_print(14,65,fm_edit_new?"New folder":"Rename file",17,fg,bg,0,1,228);
            reader_print(14,88,fm_rename,14,mint,bg,0,0,228);
            const char *keys="abcdefghijklmnopqrstuvwxyz0123456789.-_ ";
            for(int k=0;k<40;k++){int x=10+k%8*28,y=104+k/8*28;char c[2]={keys[k],0};rounded(x,y,26,26,card);reader_print(x+8,y+5,c,15,fg,card,0,0,x+25);}
            text_at(14,260,"ERASE",1,mint);text_at(87,260,"CLEAR",1,mint);text_at(163,260,"OK",1,mint);
            text_at(14,301,"CANCEL",2,mint);
        }else if(fm_panel==1){
            const char *const labels[]={"Copy","Move","Paste here","Delete","Rename","New folder"};
            for(int i=0;i<6;i++){int y=78+33*i;uint16_t tile=fm_action==i?mint:bg;rect(12,y,216,30,tile);reader_print(22,y+7,labels[i],15,fm_action==i?selection_ink():fg,tile,0,0,220);}
            text_at(12,301,"CANCEL",2,mint);
        }else{
            char location[40],info[40];snprintf(location,sizeof(location),"SD / %.33s",fm_dir);
            reader_print(12,63,location,13,muted,bg,0,0,228);
            for(int j=0;j<5&&fm_top+j<fm_count;j++){
                int i=fm_top+j,y=84+31*j;uint16_t tile=i==fm_index?mint:bg,ink=i==fm_index?selection_ink():fg;
                rounded(12,y,216,28,tile);
                if(fm_items[i].dir){rect(20,y+9,18,13,i==fm_index?ink:mint);rect(20,y+5,9,6,i==fm_index?ink:mint);}
                else{rect(23,y+5,12,18,muted);rect(25,y+10,8,1,tile);rect(25,y+13,8,1,tile);}
                reader_print(45,y+3,fm_items[i].name,13,ink,tile,0,0,223);
                if(fm_items[i].dir)strcpy(info,"Folder");
                else snprintf(info,sizeof(info),"%llu KiB",fm_items[i].bytes/1024ULL);
                reader_print(45,y+17,info,9,i==fm_index?ink:muted,tile,0,0,215);
            }
            if(!fm_count)text_at(12,112,fm_root<0&&!fm_preview?"NO MICROSD":"EMPTY FOLDER",1,muted);
            if(fm_message[0])text_at(12,245,fm_message,1,muted);
            else if(fm_clip)text_at(12,245,fm_clip==2?"MOVE READY":"COPY READY",1,mint);
            else if(fm_truncated)text_at(12,245,"LIST LIMITED TO 512 ITEMS",1,muted);
            else text_at(12,245,fm_allow?"MICROSD USER FILES":"READ ONLY",1,muted);
            list_scrollbar(84,152,fm_top,fm_count,5);
            rounded(12,261,104,24,card);rounded(124,261,104,24,card);
            reader_print(39,265,"Open",14,mint,card,0,0,111);
            reader_print(149,265,"More",14,mint,card,0,0,226);
            text_at(12,301,*fm_dir?"PARENT":"BACK",2,mint);
            reader_print(143,300,"Swipe to scroll",11,muted,bg,0,0,228);
        }
    } else if(page==BOOKS){
        rect(0,27,W,293,bg);
        centered_label(37,"Books",2,fg);rect(12,59,216,1,edge);
        char count[32];snprintf(count,sizeof(count),"%d books on microSD",book_count);
        text_at(12,64,count,1,muted);
        for(int i=0;i<4&&book_top+i<book_count;i++){
            int idx=book_top+i,y=83+i*43;
            const char *base=strrchr(books[idx],'/');base=base?base+1:books[idx];
            char name[100];book_label(idx,name,sizeof(name));
            const char *extension=strrchr(base,'.');
            uint16_t tile=idx==book_index?mint:bg,ink=idx==book_index?selection_ink():fg;
            rounded(12,y,216,39,tile);
            uint16_t covercolor=idx%2?RGB(137,93,69):RGB(44,104,99);
            rounded(18,y+5,24,29,covercolor);rect(22,y+7,1,24,RGB(236,217,170));
            rect(26,y+13,11,2,RGB(236,217,170));rect(26,y+18,8,1,RGB(236,217,170));
            char first[100];snprintf(first,sizeof(first),"%s",name);size_t cut=strlen(first);
            while(cut&&reader_measure(first,13,0,0)>170)first[--cut]=0;
            if(cut<strlen(name)){
                size_t space=cut;while(space&&first[space-1]!=' ')space--;
                if(space>cut/2){cut=space-1;first[cut]=0;}
            }
            reader_print(50,y+5,first,13,ink,tile,0,0,224);
            const char *second=name+cut;while(*second==' ')second++;
            reader_print(50,y+23,*second?second:extension?extension+1:"TEXT",10,idx==book_index?ink:muted,tile,0,0,224);
            if(idx==book_index)rect(12,y+8,2,23,mint);
        }
        if(!book_count)book_print(14,112,"Add books to SD / Books",8,12,muted,bg,0);
        if(book_status[0])text_at(12,258,book_status,1,muted);
        list_scrollbar(83,168,book_top,book_count,4);
        text_at(14,272,"Swipe to browse, tap to read",1,muted);
    } else if(page==READER){
        uint16_t paper=book_theme==3?RGB(35,43,29):book_theme==0?RGB(246,242,229):book_theme==1?RGB(255,255,255):RGB(15,18,21);
        uint16_t ink=book_theme>=2?RGB(244,240,222):RGB(38,35,32);
        uint16_t soft=book_theme==3?RGB(188,191,171):book_theme==2?RGB(145,153,153):RGB(112,103,89);
        rect(0,0,W,H,paper);
        if(book_menu){
            reader_print(14,12,"Reading settings",20,ink,paper,0,1,228);
            char label[40];
            snprintf(label,sizeof(label),"Text size: %s",book_size_label());
            reader_print(16,67,label,17,ink,paper,0,0,228);
            reader_print(16,105,book_wide?"Spacing: Airy":"Spacing: Normal",17,ink,paper,0,0,228);
            reader_print(16,143,book_theme==3?"Theme: Olive":book_theme==0?"Theme: Paper":book_theme==1?"Theme: White":"Theme: Night",17,ink,paper,0,0,228);
            reader_print(16,181,book_font==0?"Font: Smooth sans":book_font==1?"Font: Serif":"Font: Dyslexic",17,ink,paper,0,0,228);
            if(book_font&&!book_font_ok)reader_print(16,203,"Font file not installed",12,soft,paper,0,0,228);
            reader_print(16,239,book_save_error?"Resume save failed":book_allow_save?"Resume saving enabled":"Resume saving off",13,soft,paper,0,0,228);
            rect(14,276,212,1,soft);
            reader_print(16,288,"Library",16,ink,paper,0,0,110);
            reader_print(174,288,"Done",16,ink,paper,0,1,228);
        }else{
            layout_depth++;
            char heading[65];snprintf(heading,sizeof(heading),"%s",book_title[0]?book_title:"Reading");
            int title_width=reader_measure(heading,11,0,0);
            reader_print(title_width<W-32?(W-title_width)/2:16,8,heading,11,soft,paper,0,0,W-16);
            int rows=book_render_rows;
            if(!rows&&book_text[0][0])rows=book_rows();
            for(int r=0;r<rows;r++){
                int ypos=book_y[r]?book_y[r]:34+r*book_pitch();
                if(ypos+book_height()>H-32)break;
                int x=16+book_indent[r];
                if(book_bold[r]){int width=reader_measure(book_text[r],book_height(),book_font!=0,1);if(width<W-32)x=(W-width)/2;}
                reader_print(x,ypos,book_text[r],book_height(),ink,paper,book_font!=0,book_bold[r],W-16);
            }
            unsigned permille=book_bytes>0&&book_next>=0?(unsigned)((uint64_t)book_next*1000/(uint64_t)book_bytes):0;
            if(permille>1000)permille=1000;
            char progress[24],bat[16];snprintf(progress,sizeof(progress),"%u.%u%%",permille/10,permille%10);
            if(battery>=0)snprintf(bat,sizeof(bat),"%d%%",battery);else strcpy(bat,"--%");
            rect(16,H-23,W-32,1,soft);
            reader_print(16,H-17,progress,10,soft,paper,0,0,80);
            int clock_width=reader_measure(clock_label,10,0,0);
            reader_print((W-clock_width)/2,H-17,clock_label,10,soft,paper,0,0,W-60);
            int bat_width=reader_measure(bat,10,0,0);
            reader_print(W-16-bat_width,H-17,bat,10,soft,paper,0,0,W-16);
            layout_depth--;
        }
    } else if(page==NOWPLAYING){
        char position[32];snprintf(position,sizeof(position),"%d of %d",music_count?music_index+1:0,music_count);
        centered_label(64,position,1,muted);
        int artwork_y=layout_y(77);layout_depth++;
        if(cover_ok){
            for(int j=0;j<64;j++)for(int i=0;i<64;i++){
                size_t k=(size_t)(j*64+i)*3;
                rect(56+i*2,artwork_y+j*2,2,2,RGB(cover[k],cover[k+1],cover[k+2]));
            }
        }else record_placeholder(56,artwork_y);
        layout_depth--;
        centered_label(199,track_title[0]?track_title:"Choose a track",2,fg);
        centered_label(214,track_artist[0]?track_artist:"Music library",1,muted);
        centered_label(227,music_error?music_error_text(music_error):track_album[0]?track_album:"Open Music to get started",1,muted);
        rect(12,243,216,3,edge);
        if(music_length){unsigned pct=music_pos>=music_length?100:(unsigned)((uint64_t)music_pos*100/music_length);rect(12,243,(int)(216*pct/100),3,mint);}
        char elapsed[20],duration[20];snprintf(elapsed,sizeof(elapsed),"%u:%02u",music_pos/60,music_pos%60);
        if(music_length)snprintf(duration,sizeof(duration),"%u:%02u",music_length/60,music_length%60);else strcpy(duration,"--:--");
        text_at(12,251,elapsed,1,muted);text_at(W-12-ui_text_width(duration,1),251,duration,1,muted);
        const char *controls[]={"Prev",music_pid>=0?(music_pause?"Play":"Pause"):"Play","Stop","Next"};
        for(int i=0;i<4;i++){int x=12+i*55;rect(x,266,51,22,i==1?mint:card);text_at(x+5,272,controls[i],1,i==1?selection_ink():fg);}
    } else if(page==MUSIC){
        char count[32];
        snprintf(count,sizeof(count),"%d TRACKS / %s",music_count,music_list?"PLAYLIST":"SD MUSIC");
        text_at(12,64,count,1,muted);
        rounded(168,59,60,22,card);text_at(180,62,music_list?"Folders":"List",1,mint);
        for(int i=0;i<4&&music_top+i<music_count;i++){
            int idx=music_top+i,y=82+i*30;
            const char *base=strrchr(music_files[idx],'/');
            char name[33];snprintf(name,sizeof(name),"%.32s",base?base+1:music_files[idx]);
            classic_row(y,29,name,idx==music_index);
        }
        if(!music_count)text_at(12,100,"NO MUSIC TRACKS FOUND",1,muted);
        text_at(12,213,music_pid>=0?(music_switch?"SWITCHING OUTPUT":music_pending>=0?"SWITCHING TRACK":music_pause?"PAUSED":"PLAYING"):music_error?music_error_text(music_error):allow_audio?audio_active_label():"PLAYBACK LOCKED",1,mint);
        if(music_count>4){char range[48];snprintf(range,sizeof(range),"%d-%d / %d",music_top+1,music_top+4<music_count?music_top+4:music_count,music_count);text_at(12,228,range,1,muted);}
        list_scrollbar(82,116,music_top,music_count,4);
        rounded(12,240,104,33,card);rounded(124,240,104,33,card);
        reader_print(34,249,music_pid>=0?(music_pause?"Resume":"Pause"):"Play",15,fg,card,0,0,114);
        reader_print(154,249,"Stop",15,fg,card,0,0,226);
        text_at(12,278,music_shuffle?"SHUFFLE ON":"SHUFFLE OFF",1,music_shuffle?mint:muted);
        text_at(128,278,music_repeat==2?"REPEAT ONE":music_repeat==1?"REPEAT ALL":"REPEAT OFF",1,music_repeat?mint:muted);
        text_at(12,301,"BACK",2,mint);text_at(76,301,"NOW",2,mint);text_at(154,301,"LIST",2,mint);
    } else if(page==PREFERENCES) {
        text_at(12,64,"TAP TO CHANGE",1,muted);
        row(0,theme==0?"Theme: Classic":theme==1?"Theme: Night":"High contrast");
        row(1,clock24?"CLOCK: 24H":"CLOCK: 12H");
        row(2,readable?"TEXT: CONTRAST":"TEXT: NORMAL");
        row(3,saver?"POWER: SAVER":"POWER: NORMAL");
        char z[32];snprintf(z,sizeof(z),"TZ: %s",zone_names[timezone_choice]);row(4,z);
        text_at(12,234,date_label,1,muted);
        text_at(12,251,save_error?"SAVE FAILED":config_path?"SETTINGS SAVED ON CHANGE":"SESSION ONLY",1,muted);
        text_at(12,301,"BACK",2,mint);
    } else if(page==HOME) {
        text_at(12,64,"Main menu",1,muted);
        for(int i=0;i<6;i++)classic_row(80+i*34,33,page_label(home_items[i]),i==selected);
        rect(0,293,W,27,card);rect(0,293,W,1,edge);
        centered_label(298,"Now playing  >",1,mint);
    } else if(page==SETTINGS) {
        static const char *const labels[]={"Screen & brightness","Sound & volume","Wi-Fi & Bluetooth","Battery","Storage","Updates","Appearance & time"};
        text_at(12,64,"Choose a setting",1,muted);
        for(int i=0;i<7;i++) row(i,labels[i]);
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
    } else if(page==POWER){
        char buf[64];
        if(battery>=0)snprintf(buf,sizeof(buf),"%d%% remaining",battery);else strcpy(buf,"Battery unavailable");
        reader_print(14,76,buf,25,fg,bg,0,1,228);
        rounded(12,112,216,48,card);reader_print(22,120,"Battery saver",16,fg,card,0,1,220);
        reader_print(22,140,saver?"Enabled":"Standard",12,mint,card,0,0,220);
        if(ram_available>=0&&ram_total>0)snprintf(buf,sizeof(buf),"Available RAM: %ld MiB",ram_available/1024);else strcpy(buf,"Available RAM unknown");
        reader_print(14,176,buf,13,fg,bg,0,0,228);
        if(shell_rss>=0)snprintf(buf,sizeof(buf),"UI memory: %ld.%ld MiB",shell_rss/1024,(shell_rss%1024)*10/1024);else strcpy(buf,"UI memory unknown");
        reader_print(14,196,buf,12,muted,bg,0,0,228);
        reader_print(14,219,power_failed?"Idle control unavailable":power_allow?(power_blank?"Auto dim / screen off":"Auto dim enabled"):"Auto dim locked",12,muted,bg,0,0,228);
        reader_print(14,239,cpu_changed?"Adaptive CPU requested":power_cpu_allow?"CPU policy unchanged":"CPU writes locked",12,muted,bg,0,0,228);
        reader_print(14,259,"Suspend remains disabled",11,muted,bg,0,0,228);text_at(12,301,"BACK",2,mint);
    } else if(page==UPDATES){
        char version[64];snprintf(version,sizeof(version),"Installed: %u   Staged: %u",update_installed,update_current>update_installed?update_current:0);
        reader_print(12,62,version,12,muted,bg,0,0,228);
        if(update_pid>=0||update_handoff){
            int checking=update_active==U_CHECK_STABLE||update_active==U_CHECK_TEST||update_active==U_STATUS;
            int installing=update_active==U_INSTALL||update_handoff;
            int pct=update_display_percent();
            const char *heading=update_handoff?(update_error?"Recovery needs attention":"Restarting"):installing?"Preparing installation":checking?"Checking for update":"Downloading firmware";
            rounded(12,83,216,201,card);
            reader_print(20,96,heading,17,fg,card,0,1,220);
            if(!checking){
                reader_print(20,124,installing?"2 / 3  Prepare and restart":"1 / 3  Download and verify",12,muted,card,0,0,220);
                rect(20,151,200,12,edge);rect(20,151,200*pct/100,12,mint);
                char label[24];snprintf(label,sizeof(label),"%d%% prepared",pct);
                reader_print(20,171,label,19,mint,card,0,1,220);
            }else reader_print(20,153,"Checking signed release...",14,mint,card,0,1,220);
            reader_print(20,202,update_note,11,fg,card,0,0,220);
            if(update_total&&!installing){
                char label[48];snprintf(label,sizeof(label),"%u / %u KiB",update_bytes/1024,update_total/1024);
                reader_print(20,222,label,12,muted,card,0,0,220);
            }
            if(update_handoff)reader_print(20,244,update_error?"Do not manually reboot":"3 / 3  Recovery installs update",11,mint,card,0,0,220);
            else reader_print(20,244,"Keep microSD inserted",12,muted,card,0,0,220);
            reader_print(16,298,"DO NOT POWER OFF",14,mint,bg,0,1,228);
        }else if(update_confirm){
            if(update_confirm==U_INSTALL){
                char ready[64];snprintf(ready,sizeof(ready),"Install %s %u?",update_ready_channel,update_ready_seq);
                reader_print(12,94,ready,18,fg,bg,0,1,228);
                reader_print(12,130,"Keep card inserted; charge 50%+.",12,muted,bg,0,0,228);
                reader_print(12,152,"Reboots into recovery.",12,muted,bg,0,0,228);
                reader_print(12,174,"Rewrites same kernel + system.",11,muted,bg,0,0,228);
                reader_print(12,196,"Bootloader / recovery kept.",11,muted,bg,0,0,228);
            }else if(update_confirm==U_ROLLBACK){
                reader_print(12,94,"Select previous version?",18,fg,bg,0,1,228);
                reader_print(12,130,"Rollback rechecks signatures.",12,muted,bg,0,0,228);
            }else{
                char offer[64];snprintf(offer,sizeof(offer),"%s release %u",update_confirm==U_STABLE?"Stable":"Pre-release",update_offer);
                reader_print(12,94,offer,18,fg,bg,0,1,228);
                reader_print(12,130,"Download and install update?",13,fg,bg,0,0,228);
                reader_print(12,154,"Restarts automatically in recovery.",11,muted,bg,0,0,228);
                reader_print(12,176,"Keep card inserted; charge 50%+.",11,muted,bg,0,0,228);
                reader_print(12,198,"Rewrites same kernel + system.",11,muted,bg,0,0,228);
            }
            if(update_confirm==U_ROLLBACK)reader_print(12,152,"No firmware is flashed.",12,muted,bg,0,0,228);
            rounded(12,240,104,36,card);rounded(124,240,104,36,mint);
            reader_print(20,250,"Cancel",15,fg,card,0,0,112);
            reader_print(132,250,update_confirm==U_ROLLBACK?"Confirm":"Update",15,bg,mint,0,1,225);
        }else{
            const char *const labels[]={"microSD","Pre-release","Stable"};
            const char *const detail[]={"Signed package on your card","Latest experimental firmware","Latest stable firmware"};
            for(int i=0;i<3;i++){
                int y=79+i*50;rounded(12,y,216,44,card);
                if(selected==i)rect(12,y+5,2,34,mint);
                reader_print(20,y+6,labels[i],17,fg,card,0,1,226);
                reader_print(20,y+25,detail[i],11,muted,card,0,0,226);
            }
            reader_print(12,239,update_note,11,update_error?fg:mint,bg,0,0,228);
            reader_print(12,256,update_error?"Check connection and retry":update_allow?"Choose a source to update":"Updates locked",11,muted,bg,0,0,228);
            text_at(12,301,"BACK",2,mint);
        }
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
        rect(24,160,192,12,edge);rect(24,160,192*volume/(volume_extended?VOL_MAX:VOL_NORMAL),12,volume>VOL_NORMAL?RGB(235,168,66):mint);
        if(volume_extended)rect(24+192*VOL_NORMAL/VOL_MAX,157,2,18,fg);
        text_at(21,188,volume_extended?"EXTENDED RANGE - CAUTION":"100% RECOMMENDED LIMIT",1,muted);
        rounded(12,213,216,49,card);
        text_at(21,223,output_busy?"STOP MUSIC TO CHANGE":output==3?audio_active_label():"OUTPUT PREFERENCE",1,muted);
        text_at(21,239,outputs[output],2,fg);
        if(output==2){rounded(145,235,76,23,edge);text_at(150,243,"DISCONNECT",1,mint);}
        rounded(12,265,216,23,card);text_at(21,272,volume_extended?"DISABLE HIGH VOLUME":"ENABLE HIGH VOLUME",1,mint);
        text_at(12,301,"BACK",2,mint);
        if(volume_warning){
            rect(8,80,224,210,bg);rounded(12,84,216,202,card);
            text_at(20,99,"HIGH VOLUME",2,RGB(235,168,66));
            reader_print(20,130,"Loud sound can damage hearing.",13,fg,card,0,0,220);
            reader_print(20,151,"Lower volume if uncomfortable.",13,fg,card,0,0,220);
            reader_print(20,173,"Enable up to 130% this session?",12,muted,card,0,0,220);
            reader_print(20,194,"Volume will not jump.",12,muted,card,0,0,220);
            rounded(20,236,95,37,edge);rounded(125,236,95,37,edge);
            text_at(29,250,"CANCEL",1,fg);text_at(134,250,"ENABLE",1,mint);
        }
    } else if(page==WIFIUI||page==BTUI){
        if(radio_keyboard){
            reader_print(12,60,radio_ssid,16,fg,bg,0,1,178);
            if(radio_security){rounded(182,56,46,22,card);reader_print(188,60,radio_show_key?"Hide":"Show",12,mint,card,0,0,226);}
            char mask[65];size_t n=strlen(radio_key);memset(mask,'*',n);mask[n]=0;
            const char *password=radio_show_key?radio_key:mask;
            if(n>30)password+=n-30;
            reader_print(12,80,radio_security?password:"Unsecured network",13,mint,bg,0,0,228);
            if(radio_security){const char *keys=radio_keys();for(int i=0;i<40&&(size_t)i<strlen(keys);i++){int x=8+i%8*28,y=99+i/8*28;char c[2]={keys[i],0};rounded(x,y,26,26,card);reader_print(x+8,y+4,c,17,fg,card,0,0,x+25);}}
            text_at(12,254,"SHIFT",1,mint);text_at(85,254,"ERASE",1,mint);text_at(170,254,"JOIN",1,mint);
            reader_print(12,278,radio_note,10,muted,bg,0,0,228);
            text_at(12,301,"CANCEL",2,mint);
        }else{
            reader_print(12,65,radio_note,12,muted,bg,0,0,228);
            for(int j=0;j<4&&radio_top+j<radio_count;j++){
                int i=radio_top+j,y=83+j*34;uint16_t tile=i==radio_index?mint:bg,ink=i==radio_index?selection_ink():fg;rect(12,y,216,31,tile);
                reader_print(20,y+4,radio_items[i].name,14,ink,tile,0,0,222);
                char info[40];
                if(radio_mode)snprintf(info,sizeof(info),"%s",radio_items[i].address);
                else snprintf(info,sizeof(info),"%s  %d dBm",radio_items[i].security==0?"Open":radio_items[i].security==1?"WPA-PSK":"Unsupported",radio_items[i].signal);
                reader_print(20,y+18,info,10,i==radio_index?ink:muted,tile,0,0,222);
            }
            if(radio_mode){
                const char *labels[]={"UP","PAIR","DOWN","SCAN","STATUS","CONNECT"};
                for(int i=0;i<6;i++){int x=12+i%3*74,y=224+i/3*30;rounded(x,y,70,26,card);text_at(x+5,y+9,labels[i],1,mint);}
            }else{
                list_scrollbar(83,133,radio_top,radio_count,4);
                const char *labels[]={"Scan","Reconnect","Status","Disconnect"};
                for(int i=0;i<4;i++){int x=12+(i%2)*112,y=224+(i/2)*30;rounded(x,y,104,26,card);reader_print(x+7,y+6,labels[i],13,mint,card,0,0,x+101);}
            }
            text_at(12,301,"BACK",2,mint);
            char selected_audio[18];
            if(radio_mode&&(radio_count||bt_audio_selected(selected_audio)))text_at(134,301,"DISCONNECT",1,mint);
        }
    } else if(page==RADIO){
        rounded(12,81,216,60,card);
        reader_print(21,91,"Wi-Fi",18,fg,card,0,1,220);
        reader_print(21,116,radio_wifi_state()==2?"Connected":radio_wifi_state()==1?"Connecting...":"Not connected",12,muted,card,0,0,220);
        rounded(12,153,216,60,card);
        reader_print(21,163,"Bluetooth",18,fg,card,0,1,220);
        reader_print(21,188,bt_present?"Adapter detected":"Adapter not detected",12,muted,card,0,0,220);
        reader_print(12,239,radio_allow?"Manual scans only":"Radio changes locked",12,muted,bg,0,0,228);
        text_at(12,301,"BACK",2,mint);
    } else {
        rounded(12,81,216,92,card);
        if(page==DIAGNOSTICS) {
            reader_print(21,94,"PocketOS diagnostics",16,mint,card,0,1,220);
            char id[17],label[64];read_build_id(id);snprintf(label,sizeof(label),"Build: %s",id);
            reader_print(21,122,label,12,fg,card,0,0,220);
            reader_print(21,146,update_allow?"Signed updates enabled":"Updates locked",12,muted,card,0,0,220);
            snprintf(label,sizeof(label),"Battery: %d%%",battery);
            reader_print(12,185,battery>=0?label:"Battery: unknown",12,fg,bg,0,0,228);
            if(ram_available>=0)snprintf(label,sizeof(label),"Available RAM: %ld MiB",ram_available/1024);
            else strcpy(label,"Available RAM: unknown");
            reader_print(12,204,label,12,fg,bg,0,0,228);
            reader_print(12,223,config_path?"Settings: persistent":"Settings: session only",12,fg,bg,0,0,228);
            reader_print(12,242,sd_pct>=0?"microSD: mounted":"microSD: unavailable",12,fg,bg,0,0,228);
            reader_print(12,261,radio_wifi_state()==2?"Wi-Fi: connected":"Wi-Fi: not connected",12,fg,bg,0,0,228);
            reader_print(12,280,save_error?"Settings save failed":power_failed?"Display power check failed":"Kernel suspend disabled",11,muted,bg,0,0,228);
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
    navigation_bar();
}
static void open_control(void) {
    if(page==CONTROL||fm_worker>=0||update_pid>=0||update_handoff)return;
    if(radio_keyboard)radio_clear_key();
    return_page=page;return_selected=selected;page=CONTROL;selected=0;
}
static void back(void) {
    if(volume_warning){volume_warning=0;return;}
    if(page==UPDATES){if(update_pid>=0||update_handoff)return;if(update_confirm){update_confirm=U_NONE;update_clear_auto();return;}page=SETTINGS;selected=5;return;}
    if(page==CONTROL){page=return_page;selected=return_selected;return;}
    if(page==WIFIUI||page==BTUI){if(radio_keyboard){radio_clear_key();return;}if(radio_active==R_BT_SCAN)radio_cancel_scan();if(radio_pid>=0)return;page=RADIO;return;}
    if(page==READER){if(book_menu){book_menu=0;return;}book_close();page=BOOKS;return;}
    if(page==NOWPLAYING){page=MUSIC;return;}
    if(page==FILES){if(fm_worker>=0)return;if(fm_confirm||fm_panel){fm_cancel();return;}if(*fm_dir){if(!fm_up())fm_note("CANNOT OPEN PARENT");else fm_message[0]=0;return;}fm_close();page=HOME;selected=0;return;}
    enum page previous=page;page=(page>=DISPLAY?SETTINGS:HOME);selected=0;
    const enum page *items=page==SETTINGS?setting_items:home_items;
    for(int i=0;i<(page==SETTINGS?7:6);i++)if(items[i]==previous){selected=i;break;}
}
static void go_home(void){
    if(!navigation_available())return;
    if(page==FILES)fm_close();
    if(page==WIFIUI||page==BTUI)radio_clear_key();
    page=HOME;selected=0;
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
        if(i==0||i==1){radio_open(i==1);return;}
        else if(i==2)page=DISPLAY;
        else if(i==3)page=SOUND;
        else if(i==4){saver=!saver;save_error=!save_preferences();return;}
        else if(i==5)page=SETTINGS;
        else return;
        selected=0;return;
    }
    if(page==HOME&&i<6){if(home_items[i]==CONTROL){open_control();return;}page=home_items[i];if(page==MUSIC&&music_pid<0)music_scan();if(page==BOOKS)book_scan();if(page==FILES)fm_start();}
    else if(page==SETTINGS&&i<7){page=setting_items[i];if(page==UPDATES){fm_close();update_current=update_previous=0;update_confirm=U_NONE;(void)update_spawn(U_STATUS);}}else return;
    selected=0;
}
static int change_output(void){
    /* The active decoder owns its route. Do not change the preference while
       it is playing/paused, which previously made UI and hardware disagree. */
    if(music_pid>=0){output_busy=1;return 0;}
    output_busy=0;volume_revoke();output=(output+1)%4;
    save_error=!save_preferences();return 1;
}
static void set_volume_target(int v){
    if(v>VOL_NORMAL&&!volume_extended&&!smart_asleep&&!power_stage&&
       (page==SOUND||page==HOME||page==MUSIC||page==NOWPLAYING||page==CONTROL)){
        page=SOUND;volume_warning=1;
    }
    volume=vol_clamp(v,volume_extended);
    /* Never pass a UI percentage directly to an unverified DAC register. */
    save_error=!save_preferences();
    if(music_pid>=0)(void)music_volume_state();
}
static void update_choose(int i){
    if(update_pid>=0||update_handoff)return;
    if(i<0||i>2)return;
    if(!update_allow){update_message("Update actions locked");return;}
    if(music_pid>=0||book_prepare_pid>=0||fm_worker>=0||book_file||sd_worker>=0){update_message("Close active media before updating");return;}
    fm_close();
    (void)update_spawn(i==0?U_SD:i==1?U_CHECK_TEST:U_CHECK_STABLE);
}
static void tap(int x,int y) {
    if(navigation_available()&&y>=294){
        if(page==MUSIC||page==BTUI){if(x>=12&&x<78)back();else if(x>=84&&x<152){if(page==MUSIC)page=NOWPLAYING;else radio_disconnect_audio();}else if(x>=158&&x<228)go_home();}
        else if(x>=12&&x<116)back();else if(x>=124&&x<228)go_home();
        return;
    }
    if(page==SOUND&&volume_warning){
        if(y>=236&&y<273&&x>=20&&x<220){
            if(x>=125&&x<220)(void)volume_confirm(smart_jack());
            else volume_warning=0;
        }
        return;
    }
    if(page==POWER&&y>=112&&y<160){saver=!saver;save_error=!save_preferences();return;}
    if(page==UPDATES){
        if(update_pid>=0||update_handoff)return;
        if(update_confirm){if(y>=240&&y<276&&x>=12&&x<228&&(x<116||x>=124)){enum update_job job=update_confirm;update_confirm=U_NONE;if(x>=124)(void)update_accept(job);else update_clear_auto();}return;}
        if(x>=12&&x<228&&y>=79&&y<229&&(y-79)%50<44){selected=(y-79)/50;update_choose(selected);}
        else if(y>=287&&x<120)back();
        return;
    }
    if(y<26&&page!=READER){if(page==CONTROL)back();else open_control();return;}
    if(page==WIFIUI||page==BTUI){
        if(!radio_keyboard&&y>=294){
            if(page==BTUI&&x>=125)radio_disconnect_audio();
            else back();
        }else radio_tap(x,y);
        return;
    }
    if(page==RADIO&&y>=81&&y<214){radio_open(y>=153);return;}
    if(page==FILES){
        if(fm_worker>=0)return;
        if(fm_confirm){if(y>=246&&y<281&&x>=12&&x<228){if(x>=126)fm_apply();else if(x<114)fm_cancel();}return;}
        if(fm_panel==2){fm_edit_char(x,y);return;}
        if(fm_panel==1){if(x>=12&&x<228&&y>=78&&y<276&&(y-78)%33<30)fm_operation((y-78)/33);else if(y>=288)fm_cancel();return;}
        if(x>=12&&x<228&&y>=84&&y<239&&(y-84)%31<28){
            int idx=fm_top+(y-84)/31;
            if(idx<fm_count){
                int was_selected=fm_index==idx;
                fm_index=idx;
                if(fm_items[idx].dir||was_selected)fm_open_media();
            }
        }else if(y>=261&&y<285){
            if(x>=12&&x<116)fm_open_media();
            else if(x>=124&&x<228){fm_action=0;fm_panel=1;}
        }else if(y>=288&&x<120)back();
        return;
    }
    if(page==READER){
        if(book_menu){
            if(y>=275){if(x<115){book_menu=0;back();}else{book_menu=0;book_page();}return;}
            if(y>=56&&y<94)book_size=book_size%4+1;
            else if(y>=94&&y<132)book_wide=!book_wide;
            else if(y>=132&&y<170){book_theme=(book_theme+1)%4;book_dirty++;return;}
            else if(y>=170&&y<218){book_font=(book_font+1)%3;book_load_font();}
            else return;
            book_dirty++;book_depth=0;book_page_span=0;book_page();return;
        }
        if(x<80)book_turn(0);else if(x>=160)book_turn(1);else book_menu=1;
        return;
    }
    if(page==BOOKS){
        if(x>=12&&x<228&&y>=83&&y<255&&(y-83)%43<39){
            int idx=book_top+(y-83)/43;
            if(idx<book_count){book_index=idx;book_status[0]=0;(void)book_open();}
        }else if(y>=278&&y<309&&x>=12&&x<90)back();
        return;
    }
    if(page==MUSIC&&x>=168&&x<228&&y>=59&&y<81&&music_pid<0){music_list=!music_list;music_scan();save_error=!save_preferences();return;}
    if(page==MUSIC&&y>=295){
        if(x>=72&&x<145){page=NOWPLAYING;return;}
        if(x>=150&&music_pid<0){music_list=!music_list;music_scan();save_error=!save_preferences();return;}
    }
    if(page==NOWPLAYING){
        if(y>=295){back();return;}
        if(x>=12&&x<228&&y>=231&&y<=256){music_seek((x-12)*100/215);return;}
        if(x>=12&&x<228&&y>=266&&y<288){music_action((x-12)/55);return;}
    }
    if(page==MUSIC&&y>=274&&y<290){if(x<120)music_shuffle=!music_shuffle;else music_repeat=(music_repeat+1)%3;save_error=!save_preferences();return;}
    if(y>=287&&page!=HOME){back();return;}
    if(page==MUSIC){
        if(x>=12&&x<228&&y>=82&&y<202&&(y-82)%30<26){
            int idx=music_top+(y-82)/30;
            if(idx<music_count){
                if(music_pid>=0)music_choose_track(idx,0);
                else if(idx==music_index)music_start();
                else music_choose_track(idx,1);
            }
        }else if(y>=240&&y<273){
            if(x>=12&&x<116)music_action(1);
            else if(x>=124&&x<228)music_action(2);
        }
        return;
    }
    if(page==SOUND&&x>=24&&x<=216&&y>=148&&y<=183){set_volume_target((x-24)*(volume_extended?VOL_MAX:VOL_NORMAL)/192);return;}
    if(page==SOUND&&output==2&&x>=145&&x<221&&y>=235&&y<258){radio_disconnect_audio();return;}
    if(page==SOUND&&x>=12&&x<228&&y>=213&&y<263){(void)change_output();return;}
    if(page==SOUND&&x>=12&&x<228&&y>=265&&y<288){if(volume_extended)volume_revoke();else volume_warning=1;return;}
    if(page==DISPLAY&&x>=24&&x<=216&&y>=148&&y<=183&&allow_backlight){change_brightness((x-24)*100/192);return;}
    if(x<12||x>=228||y<80)return;
    if(page==HOME&&y>=294){page=NOWPLAYING;return;}
    if(page==HOME&&y<284) {
        int b=y-80;
        if(b%34<33)activate(b/34);
    } else if(page==CONTROL&&y>=82&&y<268) {
        int b=y-82;
        if(b%31<30)activate(b/31);
    } else if((page==SETTINGS||page==PREFERENCES)&&y<283&&(y-80)%29<26) activate((y-80)/29);
}
static int key(unsigned code) {
    if(page==UPDATES&&(update_pid>=0||update_handoff))return 0;
    int prev=selected,old_theme=theme,old_clock=clock24,old_readable=readable,old_saver=saver,old_zone=timezone_choice,old_brightness=brightness_pct;enum page prior=page;
    if(volume_warning){
        if(code==KEY_ESC||code==KEY_BACK){volume_warning=0;return 1;}
        if(code==KEY_VOLUMEDOWN){volume_warning=0;set_volume_target(volume-5);return 1;}
        return 0; /* repeated physical volume-up cannot confirm */
    }
    if(code==KEY_VOLUMEUP||code==KEY_VOLUMEDOWN||(page==SOUND&&(code==KEY_LEFT||code==KEY_RIGHT))){set_volume_target(volume+((code==KEY_VOLUMEUP||code==KEY_RIGHT)?5:-5));return 1;}
    if(page==POWER&&(code==KEY_ENTER||code==KEY_OK)){saver=!saver;save_error=!save_preferences();return 1;}
    if(page==UPDATES){
        if(code==KEY_ESC||code==KEY_BACK){back();return 1;}
        if(update_pid>=0||update_handoff)return 0;
        if(update_confirm){if(code==KEY_ENTER||code==KEY_OK){enum update_job job=update_confirm;update_confirm=U_NONE;(void)update_accept(job);return 1;}return 0;}
        if(code==KEY_UP||code==KEY_DOWN){selected=(selected+3+(code==KEY_UP?-1:1))%3;return 1;}
        if(code==KEY_ENTER||code==KEY_OK){update_choose(selected);return 1;}
        return 0;
    }
    if(page==WIFIUI||page==BTUI){
        if(code==KEY_ESC||code==KEY_BACK){back();return 1;}
        if(!radio_keyboard&&radio_pid<0){
            if(code==KEY_UP||code==KEY_DOWN){radio_select(code==KEY_UP?-1:1);return 1;}
            if(code==KEY_ENTER||code==KEY_OK){radio_choose();return 1;}
            if(code==KEY_MENU){(void)radio_spawn(radio_mode?R_BT_SCAN:R_WIFI_SCAN,NULL);return 1;}
        }
        return 0;
    }
    if(page==RADIO&&(code==KEY_UP||code==KEY_DOWN||code==KEY_ENTER||code==KEY_OK)){radio_open(code==KEY_DOWN);return 1;}
    if(page==FILES){
        if(fm_worker>=0)return 0;
        if(code==KEY_ESC||code==KEY_BACK){back();return 1;}
        if(fm_confirm){if(code==KEY_ENTER||code==KEY_OK){fm_apply();return 1;}return 0;}
        if(fm_panel==1){
            if(code==KEY_UP||code==KEY_DOWN){fm_action=(fm_action+6+(code==KEY_UP?-1:1))%6;return 1;}
            if(code==KEY_ENTER||code==KEY_OK){fm_operation(fm_action);return 1;}
            return 0;
        }
        if(fm_panel)return 0;
        if(code==KEY_UP||code==KEY_DOWN){fm_select(code==KEY_UP?-1:1);return 1;}
        if(code==KEY_ENTER||code==KEY_OK||code==KEY_RIGHT){fm_open_media();return 1;}
        if(code==KEY_LEFT){(void)fm_up();return 1;}
        if(code==KEY_MENU){fm_action=0;fm_panel=1;return 1;}
    }
    if(page==READER&&(code==KEY_ENTER||code==KEY_OK)){book_menu=!book_menu;return 1;}
    if(page==READER&&!book_menu&&(code==KEY_LEFT||code==KEY_RIGHT||code==KEY_PAGEUP||code==KEY_PAGEDOWN)){
        book_turn(code==KEY_RIGHT||code==KEY_PAGEDOWN);return 1;
    }
    if(page==BOOKS&&(code==KEY_UP||code==KEY_DOWN||code==KEY_ENTER||code==KEY_OK)){
        if(code==KEY_UP||code==KEY_DOWN)book_select(code==KEY_UP?-1:1);else (void)book_open();return 1;
    }
    if(page==SOUND&&(code==KEY_ENTER||code==KEY_OK)){(void)change_output();return 1;}
    if((page==MUSIC||page==NOWPLAYING)&&(code==KEY_ENTER||code==KEY_OK||code==KEY_PLAYPAUSE)){music_action(1);return 1;}
    if(page==MUSIC&&(code==KEY_UP||code==KEY_DOWN)){
        if(music_pid>=0)music_scroll(code==KEY_UP?-1:1);
        else if(music_count){int idx=music_index+(code==KEY_UP?-1:1);if(idx>=0&&idx<music_count)music_choose_track(idx,0);}
        return 1;
    }
    if((page==MUSIC||page==NOWPLAYING)&&(code==KEY_LEFT||code==KEY_RIGHT||code==KEY_UP||code==KEY_DOWN||code==KEY_STOPCD)){
        music_action(code==KEY_STOPCD?2:code==KEY_LEFT||code==KEY_UP?0:3);return 1;
    }
    if(code==KEY_MENU){if(page==CONTROL)back();else open_control();}
    else if(code==KEY_ESC||code==KEY_BACK)back();
    else if(code==KEY_ENTER||code==KEY_OK||code==KEY_SELECT)activate(selected);
    else if(page==DISPLAY&&allow_backlight&&brightness_pct>=0&&(code==KEY_LEFT||code==KEY_RIGHT)) {change_brightness(brightness_pct+(code==KEY_RIGHT?10:-10));return 1;}
    else if(page==HOME||page==SETTINGS||page==CONTROL||page==PREFERENCES) {
        int count=page==SETTINGS?7:page==PREFERENCES?5:6;
        int step=1;
        if(code==KEY_DOWN)selected=(selected+step)%count;
        else if(code==KEY_UP)selected=(selected+count-step)%count;
        else if(code==KEY_RIGHT)selected=(selected+1)%count;
        else if(code==KEY_LEFT)selected=(selected+count-1)%count;
    }
    return prev!=selected||prior!=page||old_theme!=theme||old_clock!=clock24||old_readable!=readable||old_saver!=saver||old_zone!=timezone_choice||old_brightness!=brightness_pct;
}
static int sample_battery(void) {
    return (int)hw_battery_read("").percent;
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
    /* PocketOS mounts existing NAND userdata here, not the old vendor path. */
    if(!mounted_usage("/run/pocketos-data",&internal_pct,&internal_free))
        (void)mounted_usage("/usr/data",&internal_pct,&internal_free);
    if(hw_sd_mounted(""))(void)mounted_usage("/mnt/sd_0",&sd_pct,&sd_free);
}
static int ppm(const char *path) {
    FILE *f=fopen(path,"wb");if(!f)return 1;
    if(fprintf(f,"P6\n%d %d\n255\n",CW,CH)<0){fclose(f);return 1;}
    unsigned char line[CW*3];
    for(int y=0;y<CH;y++){
        for(int x=0;x<CW;x++){uint16_t v=canvas[y*CW+x];line[x*3]=(unsigned char)(((v>>11)&31)*255/31);line[x*3+1]=(unsigned char)(((v>>5)&63)*255/63);line[x*3+2]=(unsigned char)((v&31)*255/31);}
        if(fwrite(line,1,sizeof(line),f)!=sizeof(line)){fclose(f);return 1;}
    }
    return fclose(f)!=0;
}
struct fb {int fd;struct fb_var_screeninfo v;struct fb_fix_screeninfo f;unsigned char *mem;unsigned bytes,vw,vh,ox,oy;int pan_disabled;};
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
    b->vw=b->v.xres;b->vh=b->v.yres;
    if(!b->vw||!b->vh)goto bad;
    b->ox=(b->v.xres-b->vw)/2;b->oy=(b->v.yres-b->vh)/2;
    if(unblank_display&&ioctl(b->fd,FBIOBLANK,FB_BLANK_UNBLANK)<0){perror("framebuffer unblank");goto bad;}
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
    int double_buffer=!b->pan_disabled&&(b->v.yoffset==0||b->v.yoffset==b->v.yres)&&
        (uint64_t)b->v.yres*2<=b->v.yres_virtual&&
        (uint64_t)b->v.yres*2*b->f.line_length<=b->f.smem_len;
    unsigned target=double_buffer?(b->v.yoffset?0:b->v.yres):b->v.yoffset;
    for(unsigned y=0;y<b->v.yres;y++){
        unsigned char *line=b->mem+((size_t)y+target)*b->f.line_length+(size_t)b->v.xoffset*b->bytes;
        for(unsigned x=0;x<b->v.xres;x++){
            uint16_t c=bg;
            if(x>=b->ox&&x<b->ox+b->vw&&y>=b->oy&&y<b->oy+b->vh)c=canvas[((y-b->oy)*CH/b->vh)*CW+(x-b->ox)*CW/b->vw];
            uint32_t pixel=channel((c>>11)&31,31,b->v.red)|channel((c>>5)&63,63,b->v.green)|channel(c&31,31,b->v.blue);
            if(b->v.transp.length)pixel|=((1u<<b->v.transp.length)-1)<<b->v.transp.offset;
            for(unsigned i=0;i<b->bytes;i++)line[x*b->bytes+i]=(unsigned char)(pixel>>(8*i));
        }
    }
#ifdef __mips__
    if(cacheflush(b->mem,(int)b->f.smem_len,DCACHE)<0){static int warned;if(!warned){perror("framebuffer cache flush");warned=1;}}
#endif
    /* Sonix's working backend renders the other page, then flips on vsync.
     * Reuse the existing two-page mode; never reset it during a handoff. */
    if(double_buffer){
        struct fb_var_screeninfo next=b->v;next.yoffset=target;
        if(ioctl(b->fd,FBIOPAN_DISPLAY,&next)==0)b->v.yoffset=target;
        else {
            int error=errno;
            /* R1 returns EBUSY on the observed two-page mode. Do not keep
             * rendering twice and issuing a failing ioctl on each redraw. */
            if(error==EBUSY||error==EINVAL||error==ENOTTY||error==EOPNOTSUPP)b->pan_disabled=1;
            static int warned;if(!warned){errno=error;perror("framebuffer page flip; using current page");warned=1;}
            for(unsigned y=0;y<b->v.yres;y++){
                size_t x=(size_t)b->v.xoffset*b->bytes;
                memcpy(b->mem+((size_t)b->v.yoffset+y)*b->f.line_length+x,
                       b->mem+((size_t)target+y)*b->f.line_length+x,(size_t)b->v.xres*b->bytes);
            }
#ifdef __mips__
            (void)cacheflush(b->mem,(int)b->f.smem_len,DCACHE);
#endif
        }
    }
}
/* Native splash reuses the existing RGB565 canvas and font; no image asset. */
static int boot_splash=0;
#include "pocketos-keys.h"
static int exclusive_input=0;
static int trace_touch=0;
static volatile sig_atomic_t shell_stopping=0;
static void stop_shell(int sig){(void)sig;shell_stopping=1;}
static void boot_frame(int frame){
    bg=RGB(10,17,19);fg=RGB(239,246,246);mint=RGB(109,226,182);
    rect(0,0,W,H,bg);
    rounded(96,82,48,48,RGB(25,48,44));
    rounded(104,90,32,32,mint);
    rect(111,97,18,4,bg);rect(111,105,4,11,bg);rect(119,104,10,4,bg);
    const char *name="PocketOS";
    reader_print((W-reader_measure(name,29,0,1))/2,151,name,29,fg,bg,0,1,W);
    const char *subtitle="A little more pocket.";
    reader_print((W-reader_measure(subtitle,13,0,0))/2,190,subtitle,13,RGB(151,171,179),bg,0,0,W);
    rounded(82,230,76,5,RGB(47,62,70));
    rect(82,230,19*(frame+1),5,mint);
}
static int native(const char *input) {
    struct fb b={0};if(!fb_open(&b)){fputs("Unsupported or unavailable framebuffer\n",stderr);return 1;}
    int fd=open(input,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0){perror("input");munmap(b.mem,b.f.smem_len);close(b.fd);return 1;}
    if(exclusive_input&&ioctl(fd,EVIOCGRAB,1)<0){perror("exclusive input");close(fd);munmap(b.mem,b.f.smem_len);close(b.fd);return 1;}
    struct input_absinfo ax={0},ay={0};
    int mt=ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&ax)==0&&ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&ay)==0;
    int touch=mt||(ioctl(fd,EVIOCGABS(ABS_X),&ax)==0&&ioctl(fd,EVIOCGABS(ABS_Y),&ay)==0);
    touch=touch&&ax.maximum>ax.minimum&&ay.maximum>ay.minimum;
    char input_name[128]={0};(void)ioctl(fd,EVIOCGNAME(sizeof(input_name)),input_name);
    int corrected=touch?touch_calibrate("",input_name,&ax,&ay,b.v.xres,b.v.yres):0;
    struct volume_input volume_inputs[VOLUME_INPUTS]={0};
    int volume_count=volume_input_open(volume_inputs,input);
    smart_allow=smart_ready();
    int power_fd=smart_allow?suspend_input_open():-1;struct suspend_key power_key={0};
    if(power_fd<0)smart_allow=0;
    fprintf(stderr,"smart-sleep=%s key=%d\n",smart_allow?"ready":"gated",power_fd);
    fprintf(stderr,"display=%ux%u viewport=%ux%u offset=%u,%u touch=%s ranges=%d,%d:%d,%d r1_calibration=%d\n",
        b.v.xres,b.v.yres,b.vw,b.vh,b.ox,b.oy,input_name,ax.minimum,ax.maximum,ay.minimum,ay.maximum,corrected);
    int rx=0,ry=0,have_x=0,have_y=0,down=0,pending=0,slot=0,dropped=0;
    int start_y=-1;
    struct sigaction stop={.sa_handler=stop_shell};sigemptyset(&stop.sa_mask);
    (void)sigaction(SIGTERM,&stop,NULL);(void)sigaction(SIGINT,&stop,NULL);
    if(boot_splash){
        /* At most three short delays. A pending key/touch immediately skips splash. */
        for(int f=0;f<4&&!shell_stopping;f++){
            boot_frame(f);present(&b);
            if(f<3){struct pollfd skip={.fd=fd,.events=POLLIN};if(poll(&skip,1,80)!=0)break;}
        }
    }
    int clock_changed=0;
    (void)update_clock(&clock_changed);
    load_preferences();(void)update_clock(&clock_changed);
    battery=sample_battery();sample_quick();power_memory();
    if(allow_backlight&&brightness_saved>=10&&backlight_dir[0]){
        /* Restore without writing preferences again at every boot. */
        if(write_backlight(backlight_dir,brightness_saved))sample_quick();
        else brightness_error=1;
    }
    render();present(&b);
    long long last_input=mono_ms(),next_sample=last_input+60000;
    long long next_media=last_input,next_library=last_input+45000,next_audio=last_input,next_clock=last_input;
    int wake_gesture=0,old_saver=-1;
    (void)signal(SIGPIPE,SIG_IGN);
    while(!shell_stopping){
        long long now=mono_ms();
        int busy=radio_pid>=0||update_pid>=0||update_handoff||book_prepare_pid>0||fm_worker>=0||sd_worker>=0;
        int saving=saver||smart_asleep;
        if(saving!=old_saver){cpu_policy(saving);old_saver=saving;}
        /* Do not switch off the display while a transfer is in progress. */
        if(busy&&power_stage&&power_wake()){smart_asleep=0;render();present(&b);}
        if(!smart_asleep&&power_allow&&now>=0&&last_input>=0&&!down&&!pending&&backlight_dir[0])
            power_idle(idle_target((now-last_input)/1000,saver,page==READER,busy,power_blank));
        int report=music_read_report();
        int route_changed=0;
        if((music_pid>=0||volume_extended)&&now>=0&&now>=next_audio){route_changed=smart_audio_guard();next_audio=now+500;}
        int reaped=music_reap();
        int book_changed=smart_asleep?0:book_prepare_reap(),fm_changed=fm_poll_transfer();
        int radio_changed=smart_asleep?0:radio_poll(),update_changed=smart_asleep?0:update_poll(),sample_changed=0;
        int media_changed=sd_refresh_poll();
        if((!smart_asleep||music_pid>=0)&&!update_handoff&&update_pid<0&&fm_worker<0&&sd_worker<0&&now>=0&&now>=next_media){
            media_changed|=media_probe();
            if(!smart_asleep)sd_refresh_start();
            next_media=now+((page==MUSIC||page==BOOKS||page==FILES||page==READER||music_pid>=0)?5000:30000);
        }
        /* Only refresh a visible idle library, never invalidate the live queue
           or repeatedly traverse the card while the screen is dimmed. */
        if(!smart_asleep&&!power_stage&&!busy&&!down&&!pending&&media_online==1&&now>=0&&now>=next_library){
            if(page==MUSIC&&music_pid<0)media_changed|=music_refresh();
            if(page==BOOKS&&book_prepare_pid<0)media_changed|=book_refresh();
            next_library=now+(saver?180000:90000);
        }
        if(!smart_asleep&&now>=0&&now>=next_sample){
            int n=sample_battery(),oldwifi=wifi_present,oldbt=bt_present,oldaudio=audio_present;
            int oldip=internal_pct,oldsp=sd_pct,oldbr=brightness_pct;
            unsigned long long i=internal_free,j=sd_free;
            long ram=ram_available,rss=shell_rss;
            sample_quick();power_memory();
            sample_changed=n!=battery||oldwifi!=wifi_present||oldbt!=bt_present||oldaudio!=audio_present||
                oldip!=internal_pct||oldsp!=sd_pct||oldbr!=brightness_pct||i!=internal_free||j!=sd_free||
                (page==POWER&&(ram!=ram_available||rss!=shell_rss));
            battery=n;next_sample=now+(saver?120000:60000);
        }
        int timeout=saver?60000:30000;
        clock_changed=0;
        /* Audio reports/input can wake poll frequently. Only format the clock
         * on its own deadline or explicit user interaction. */
        if(!smart_asleep&&!power_stage&&now>=0&&now>=next_clock){
            next_clock=now+update_clock(&clock_changed);
        }
        if(!smart_asleep&&!power_stage)timeout=limit_wait(timeout,now,next_clock);
        if(!power_stage&&((book_changed&&(page==BOOKS||page==READER))||(radio_changed&&(page==WIFIUI||page==BTUI))||
              (update_changed&&page==UPDATES)||(fm_changed&&page==FILES)||((report||reaped||route_changed)&&(page==MUSIC||page==NOWPLAYING||page==SOUND))||
              media_changed||(page!=READER&&(clock_changed||sample_changed)))){render();present(&b);}
        if(radio_pid>=0&&timeout>200)timeout=200;
        if(radio_wifi_wait&&timeout>250)timeout=250;
        if(update_pid>=0&&timeout>500)timeout=500;
        if((book_prepare_pid>=0||fm_worker>=0||sd_worker>=0)&&timeout>200)timeout=200;
        if((music_pid>=0||volume_extended)&&timeout>500)timeout=500;
        if(music_pid>=0&&music_report<0&&timeout>1000)timeout=1000;
        timeout=limit_wait(timeout,now,next_sample);
        if(!smart_asleep&&!update_handoff&&update_pid<0&&fm_worker<0&&sd_worker<0)timeout=limit_wait(timeout,now,next_media);
        if(!smart_asleep&&!power_stage&&!busy&&!down&&!pending&&media_online==1&&(page==MUSIC||page==BOOKS))
            timeout=limit_wait(timeout,now,next_library);
        if(power_allow&&!power_failed&&allow_backlight&&!busy&&now>=0&&last_input>=0&&backlight_dir[0]){
            long dim=page==READER?(saver?90:180):(saver?25:60);
            long blank=page==READER?(saver?180:300):(saver?60:120);
            if(!down&&!pending){
                if(!power_stage)timeout=limit_wait(timeout,now,last_input+dim*1000);
                else if(power_stage==1&&power_blank)timeout=limit_wait(timeout,now,last_input+blank*1000);
            }
        }
        /* No periodic status or clock wakeups when manually asleep. Only
         * poll the headset during active audio, at most every 500 ms. */
        if(smart_asleep)timeout=(volume_extended||music_pid>=0)?500:-1;
        struct pollfd p[3+VOLUME_INPUTS]={{.fd=fd,.events=POLLIN},{.fd=music_report,.events=POLLIN}};
        p[2+volume_count]=(struct pollfd){.fd=power_fd,.events=POLLIN};
        for(int k=0;k<volume_count;k++)p[2+k]=(struct pollfd){.fd=volume_inputs[k].fd,.events=POLLIN};
        int rc=poll(p,(nfds_t)(3+volume_count),timeout);
        if(rc<0){if(errno==EINTR)continue;break;}
        if(!rc)continue;
        if(p[2+volume_count].revents&(POLLERR|POLLHUP|POLLNVAL)){
            close(power_fd);power_fd=-1;
            if(smart_asleep){if(!power_wake())break;smart_asleep=0;render();present(&b);}
            smart_allow=0;
        }
        if(power_fd>=0&&(p[2+volume_count].revents&POLLIN)){
            struct input_event events[16];ssize_t size=read(power_fd,events,sizeof(events));int request=0;
            if(size>0)for(size_t j=0;j<(size_t)size/sizeof(*events);j++)request|=suspend_key_event(&power_key,&events[j],mono_ms());
            if(request){
                if(smart_asleep){
                    if(!power_wake()){fputs("Smart Sleep backlight restore failed\n",stderr);continue;}
                    smart_asleep=0;
                    last_input=mono_ms();next_sample=next_media=next_library=next_clock=last_input;
                    down=pending=have_x=have_y=0;start_y=-1;wake_gesture=0;
                    struct input_event discarded[16];
                    for(int j=0;j<16;j++)if(read(fd,discarded,sizeof(discarded))<=0)break;
                    (void)update_clock(&clock_changed);render();present(&b);continue;
                }
                int blocked=radio_pid>=0||update_pid>=0||update_handoff||book_prepare_pid>0||fm_worker>=0||sd_worker>=0||down||pending;
                if(!blocked&&smart_allow&&smart_unlocked()&&suspend_key_released(power_fd)){
                    book_save();
                    if((!book_file||!book_allow_save||!book_save_error)&&power_wake()){
                        power_idle(2);
                        if(power_stage==2){volume_warning=0;smart_asleep=1;smart_audio_guard();continue;}
                    }
                }else fputs("Smart Sleep locked or busy\n",stderr);
            }
        }
        int volume_changed=0;
        for(int k=0;k<volume_count;k++){
            if(p[2+k].revents&(POLLERR|POLLHUP|POLLNVAL)){
                close(volume_inputs[k].fd);volume_inputs[k].fd=-1;continue;
            }
            if(!(p[2+k].revents&POLLIN))continue;
            struct input_event events[16];ssize_t size=read(volume_inputs[k].fd,events,sizeof(events));
            if(size<=0)continue;
            for(size_t j=0;j<(size_t)size/sizeof(*events);j++){
                int delta=volume_input_event(&volume_inputs[k],&events[j],mono_ms());
                if(delta&&(!smart_asleep||(music_pid>=0&&!music_pause&&music_keep_audio(output,smart_jack())))){
                    if(delta<0)volume_warning=0;
                    if(!volume_warning)set_volume_target(volume+delta*5);
                    volume_changed=1;
                }
            }
        }
        if(volume_changed&&!power_stage){render();present(&b);}
        if(p[1].revents&(POLLHUP|POLLERR|POLLNVAL)){
            (void)music_read_report();if(music_report>=0)close(music_report);music_report=-1;
            if(!power_stage&&(page==MUSIC||page==NOWPLAYING)){render();present(&b);}
        }
        if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL))break;
        if(!(p[0].revents&POLLIN))continue;
        struct input_event e[16];ssize_t got=read(fd,e,sizeof(e));
        if(got<0){if(errno==EAGAIN||errno==EINTR)continue;break;}if(!got)break;
        if(smart_asleep)continue; /* drain touches but never activate UI */
        int changed=0;
        for(size_t i=0;i<(size_t)got/sizeof(*e);i++){
            struct input_event v=e[i];
            if((v.type==EV_KEY&&v.value==1)||v.type==EV_ABS){
                long long activity=mono_ms();if(activity>=0)last_input=activity;
                if(power_stage){
                    if(power_wake()){changed=1;next_clock=activity;wake_gesture=1;down=pending=have_x=have_y=0;start_y=-1;}
                    else power_failed=1;
                }
            }
            /* Consume the wake gesture instead of accidentally activating a control. */
            if(wake_gesture){
                if((v.type==EV_KEY&&v.value==0)||(v.type==EV_ABS&&v.code==ABS_MT_TRACKING_ID&&v.value<0))wake_gesture=0;
                continue;
            }
            if(v.type==EV_SYN&&v.code==SYN_DROPPED){dropped=1;pending=down=have_x=have_y=0;start_y=-1;continue;}
            if(dropped){if(v.type==EV_SYN&&v.code==SYN_REPORT)dropped=0;continue;}
            if(v.type==EV_ABS&&v.code==ABS_MT_SLOT){slot=v.value;continue;}
            if(touch&&v.type==EV_ABS&&(!mt||slot==0)){
                if(v.code==(mt?ABS_MT_POSITION_X:ABS_X)){rx=v.value;have_x=1;}
                else if(v.code==(mt?ABS_MT_POSITION_Y:ABS_Y)){ry=v.value;have_y=1;}
                else if(mt&&v.code==ABS_MT_TRACKING_ID){if(v.value>=0){down=1;have_x=have_y=0;start_y=-1;}else if(down){pending=1;down=0;}}
            }
            /* R1 hyn_ts uses type-A MT and BTN_TOUCH release, not MT slots. */
            if(touch&&v.type==EV_KEY&&v.code==BTN_TOUCH){if(v.value==1){if(!down){down=1;start_y=-1;}}else if(!v.value&&down){pending=1;down=0;}}
            if(v.type==EV_KEY&&v.value==1&&v.code!=BTN_TOUCH)changed|=key(v.code);
            if(v.type==EV_SYN&&v.code==SYN_REPORT&&down&&have_y&&start_y<0){
                start_y=touch_canvas(ry,ay.minimum,ay.maximum,b.v.yres,b.oy,b.vh,LAYOUT_H);
            }
            if(v.type==EV_SYN&&v.code==SYN_REPORT&&pending){
                if(have_x&&have_y){
                    int x=touch_canvas(rx,ax.minimum,ax.maximum,b.v.xres,b.ox,b.vw,W);
                    int y=touch_canvas(ry,ay.minimum,ay.maximum,b.v.yres,b.oy,b.vh,LAYOUT_H);
                    if(trace_touch){fprintf(stderr,"touch raw=%d,%d canvas=%d,%d page=%d\n",rx,ry,x,y,(int)page);fflush(stderr);}
                    if(x>=0&&y>=0){
                        if(start_y>=0&&start_y<26&&y-start_y>=35&&page!=CONTROL&&page!=UPDATES)open_control();
                        else if(page==CONTROL&&start_y>=0&&start_y-y>=40)back();
                        else if(page==FILES&&fm_worker<0&&!fm_panel&&!fm_confirm&&start_y>=84&&start_y<239&&
                                (start_y-y>=25||y-start_y>=25)){
                            int delta=start_y-y;int steps=(abs(delta)+15)/31;
                            if(steps<1)steps=1;
                            fm_scroll(delta>0?steps:-steps);
                        }else if(page==MUSIC&&start_y>=82&&start_y<202&&abs(start_y-y)>=25){
                            int delta=start_y-y,steps=(abs(delta)+15)/30;if(steps<1)steps=1;
                            music_scroll(delta>0?steps:-steps);
                        }else if(page==WIFIUI&&!radio_keyboard&&radio_pid<0&&start_y>=83&&start_y<219&&abs(start_y-y)>=25){
                            int delta=start_y-y,steps=(abs(delta)+17)/34;
                            radio_top=scroll_top(radio_top,radio_count,4,delta>0?steps:-steps);
                            if(radio_index<radio_top)radio_index=radio_top;
                            if(radio_count&&radio_index>=radio_top+4)radio_index=radio_top+3;
                        }else if(page==BOOKS&&start_y>=83&&start_y<255&&abs(start_y-y)>=30){
                            int delta=start_y-y,steps=(abs(delta)+21)/43;if(steps<1)steps=1;
                            book_scroll(delta>0?steps:-steps);
                        }else tap(x,y);
                        changed=1;
                    }
                }
                pending=0;have_x=have_y=0;start_y=-1;
            }
        }
        if(changed){int interval=update_clock(&clock_changed);long long t=mono_ms();if(t>=0)next_clock=t+interval;render();present(&b);}
    }
    if(music_pid>=0){if(music_fd>=0)close(music_fd);if(music_report>=0)close(music_report);(void)kill(music_pid,SIGTERM);}
    if(book_prepare_pid>0)(void)kill(book_prepare_pid,SIGTERM);
    if(sd_worker>=0){while(waitpid(sd_worker,NULL,0)<0&&errno==EINTR){}sd_worker=-1;}
    if(fm_worker>=0){(void)kill(fm_worker,SIGTERM);while(waitpid(fm_worker,NULL,0)<0&&errno==EINTR){}close(fm_worker_fd);fm_worker=-1;}
    for(int k=0;k<volume_count;k++)if(volume_inputs[k].fd>=0)close(volume_inputs[k].fd);
    if(power_fd>=0)close(power_fd);
    (void)power_wake();(void)cpu_restore();update_close();radio_close();fm_close();book_close();close(fd);munmap(b.mem,b.f.smem_len);close(b.fd);return shell_stopping?0:1;
}
static int selftest(void) {
    char label[16];
    struct tm test={.tm_hour=0,.tm_min=5};
    format_clock(&test,label);if(strcmp(label,"12:05 AM"))return 1;
    test.tm_hour=12;format_clock(&test,label);if(strcmp(label,"12:05 PM"))return 1;
    test.tm_hour=23;test.tm_min=59;format_clock(&test,label);if(strcmp(label,"11:59 PM"))return 1;
    format_clock(NULL,label);if(strcmp(label,"--:--"))return 1;
    page=HOME;selected=0;tap(20,80);if(page!=MUSIC)return 1;back();
    tap(140,190);if(page!=SETTINGS)return 1;tap(20,180);if(page!=POWER)return 1;back();
    if(page!=SETTINGS)return 1;
    back();
    if(page!=HOME||selected!=3)return 1;
    selected=0;key(KEY_DOWN);if(selected!=1)return 1;key(KEY_ENTER);if(page!=BOOKS)return 1;
    back();tap(100,10);if(page!=CONTROL)return 1;
    tap(20,210);if(!saver||page!=CONTROL)return 1;
    tap(20,300);if(page!=HOME)return 1;
    tap(140,190);if(page!=SETTINGS)return 1;
    tap(20,260);if(page!=PREFERENCES)return 1;
    tap(20,110);if(!clock24)return 1;
    format_clock(&test,label);if(strcmp(label,"23:59"))return 1;
    tap(20,82);if(theme!=1)return 1;
    tap(20,200);if(timezone_choice!=1)return 1;
    page=SOUND;tap(120,162);if(volume!=50)return 1;
    key(KEY_VOLUMEUP);if(volume!=55)return 1;
    set_volume_target(100);key(KEY_VOLUMEUP);if(!volume_warning||volume!=100||volume_extended)return 1;
    key(KEY_VOLUMEUP);if(!volume_warning||volume!=100)return 1;
    tap(50,250);if(volume_warning||volume_extended)return 1;
    volume_warning=1;if(volume_confirm(0)||volume_extended||volume_warning)return 1;
    volume_warning=1;if(!volume_confirm(1)||!volume_extended||volume_warning||volume!=100)return 1;
    set_volume_target(140);if(volume!=130)return 1;
    output=0;smart_audio_apply(0);if(volume_extended||volume!=100)return 1;
    smart_asleep=1;set_volume_target(105);smart_asleep=0;if(volume_warning||volume!=100)return 1;
    output=0;tap(30,230);if(output!=1)return 1;
    music_count=3;music_index=2;music_repeat=1;if(music_next_index()!=0)return 1;
    music_repeat=2;if(music_next_index()!=2)return 1;
    music_repeat=0;music_shuffle=1;if(music_next_index()==2)return 1;
    music_count=8;music_index=0;music_top=0;music_scroll(100);if(music_top!=4)return 1;
    music_scroll(-100);if(music_top!=0)return 1;
    music_start_at=123;music_choose_track(2,0);if(music_index!=2||music_start_at)return 1;
    music_length=200;music_seek(50);if(music_pos!=100||music_start_at!=100||music_pid>=0)return 1;
    music_action(2);if(music_pos||music_start_at)return 1;
    /* Active-route guard: fake PID is never signalled by change_output. */
    music_pid=getpid();int route=output;if(change_output()||output!=route||!output_busy)return 1;
    music_pid=-1;output_busy=0;
    book_count=8;book_index=0;book_select(-99);if(book_index!=0)return 1;
    book_select(99);if(book_index!=7||book_top!=4)return 1;
    book_scroll(-99);if(book_top!=0||book_index!=7)return 1;
    book_scroll(99);if(book_top!=4)return 1;
    page=BOOKS;tap(202,293);if(page!=BOOKS||book_index!=7)return 1; /* old Next area is inert */
    fm_count=20;fm_top=0;fm_index=0;fm_scroll(99);if(fm_top!=15||fm_index!=15)return 1;
    fm_scroll(-99);if(fm_top!=0||fm_index!=4)return 1;
    fm_count=fm_index=fm_top=0;
    char files[2][256]={"a","b"};uint32_t fh=library_hash(files,2);
    if(fh==library_hash(files,1))return 1;
    strcpy(files[1],"c");if(fh==library_hash(files,2))return 1;
    page=MUSIC;tap(100,305);if(page!=NOWPLAYING)return 1;back();if(page!=MUSIC)return 1;
    render();return 0;
}
static int friendly_navigation_test(void){
    page=SETTINGS;selected=0;tap(200,305);if(page!=HOME)return 1;
    tap(140,120);if(page!=BOOKS)return 1;
    tap(200,305);if(page!=HOME)return 1;
    tap(30,230);if(page!=CONTROL)return 1;
    back();if(page!=HOME)return 1;
    tap(150,270);if(page!=DIAGNOSTICS)return 1;
    tap(30,305);if(page!=HOME||selected!=5)return 1;
    page=DISPLAY;tap(30,305);if(page!=SETTINGS||selected!=0)return 1;
    page=MUSIC;music_pid=getpid();tap(200,305);
    if(page!=HOME||music_pid!=getpid())return 1;
    music_pid=-1;
    page=UPDATES;update_handoff=1;tap(200,305);if(page!=UPDATES)return 1;update_handoff=0;
    page=FILES;fm_worker=getpid();tap(200,305);if(page!=FILES)return 1;fm_worker=-1;
    page=HOME;selected=0;
    puts("PASS: Home/Back navigation, shortcuts, playback and busy guards");return 0;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--test-friendly-navigation"))return friendly_navigation_test();
    if(argc==3&&!strcmp(argv[1],"--test-metadata")){
        FILE *f=regular_file(argv[2]);if(!f)return 1;
        char title[65]="",artist[65]="",album[65]="";uint32_t duration=0;
        meta_read(f,title,artist,album,&duration);fclose(f);
        printf("%s\n%s\n%s\n%u\n",title,artist,album,duration);return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--test-reader-fonts")){
        book_dir=argv[2];
        for(int font=1;font<=2;font++){
            book_font=font;book_load_font();
            if(book_font_ok!=2||reader_advance('W',16,1,0)<=reader_advance('i',16,1,0)){book_close();return 1;}
        }
        book_close();return 0;
    }
    if(argc==5&&!strcmp(argv[1],"--preview-book")){
        char *end;errno=0;long long offset=strtoll(argv[3],&end,10);
        if(errno||end==argv[3]||*end||offset<0)return 2;
        book_file=regular_file(argv[2]);struct stat st;
        if(!book_file||fstat(fileno(book_file),&st)||st.st_size<1||st.st_size>67108864||offset>=st.st_size)return 2;
        book_bytes=st.st_size;book_pos=(off_t)offset;book_reflow=1;strcpy(book_title,"Book preview");
        book_page();page=READER;render();int rc=ppm(argv[4]);fclose(book_file);return rc;
    }
    if(argc==3&&!strcmp(argv[1],"--test-touch")){
        struct input_absinfo x={.maximum=720},y={.maximum=1280};
        int corrected=touch_calibrate(argv[2],"hyn_ts",&x,&y,480,800);
        printf("corrected=%d\nback_x=%d\nback_y=%d\noutside=%d\n",corrected,
            touch_canvas(24,0,x.maximum,480,0,480,W),touch_canvas(760,0,y.maximum,800,0,800,LAYOUT_H),
            touch_canvas(y.maximum+1,0,y.maximum,800,0,800,LAYOUT_H));return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--test-hardware")){
        struct hw_battery h=hw_battery_read(argv[2]);
        printf("battery_percent=%ld\nbattery_ambiguous=%d\nbattery_source=%s\nsd_mounted=%d\n",h.percent,h.ambiguous,h.source,hw_sd_mounted(argv[2]));return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS shell v1");return 0;}
    if(argc==2&&!strcmp(argv[1],"--self-test"))return selftest();
    if(argc==4&&!strcmp(argv[1],"--boot-preview")){
        if(strlen(argv[2])!=1||argv[2][0]<'0'||argv[2][0]>'3')return 2;
        boot_frame(argv[2][0]-'0');return ppm(argv[3]);
    }
    if(argc==2&&!strcmp(argv[1],"--test-power")){
        if(idle_target(24,1,0,0,1)||idle_target(25,1,0,0,1)!=1||
           idle_target(60,1,0,0,1)!=2||idle_target(60,1,0,0,0)!=1||
           idle_target(60,1,1,0,1)||idle_target(600,1,0,1,1))return 1;
        if(limit_wait(60000,1000,2500)!=1500||limit_wait(30000,1000,2000)!=1000)return 1;
        if(!governor_listed("performance schedutil ondemand","schedutil")||governor_listed("performance schedutil","sched"))return 1;
        power_memory();if(ram_total<=0)return 1;
        power_idle(2);if(power_stage)return 1;
        cpu_policy(1);if(cpu_changed)return 1;
        return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--test-update-ui")){
        update_parse("POCKETOS INSTALLED 6");
        if(update_installed!=6)return 1;
        update_parse("POCKETOS SELECTION 8 7");
        update_parse("POCKETOS PROGRESS 60");
        update_parse("POCKETOS PROGRESS 1000");
        update_parse("POCKETOS PROGRESS 20");
        update_parse("POCKETOS PHASE DOWNLOAD");
        update_parse("POCKETOS BYTES 512 4096");
        update_parse("POCKETOS BYTES 128 4096");
        update_parse("POCKETOS BYTES 9999 4096");
        update_parse("POCKETOS BYTES 1024 8192");
        if(update_bytes!=512||update_total!=4096||strcmp(update_note,"Downloading to microSD"))return 1;
        update_parse("POCKETOS PHASE VERIFY");
        if(strcmp(update_note,"Verifying package checksums"))return 1;
        if(update_current!=8||update_previous!=7||update_percent!=60)return 1;
        if(update_spawn(U_GITHUB)||update_pid>=0)return 1;
        page=UPDATES;update_allow=1;
        update_choose(3);if(update_confirm||update_pid>=0)return 1;
        update_active=U_CHECK_STABLE;
        update_parse("POCKETOS READY 8 test");if(update_ready_seq)return 1;
        update_parse("POCKETOS READY 8 stable");
        if(update_ready_seq!=8||strcmp(update_ready_channel,"stable"))return 1;
        update_auto=1;update_auto_seq=8;strcpy(update_auto_channel,"test");if(update_auto_matches())return 1;
        strcpy(update_auto_channel,"stable");if(!update_auto_matches())return 1;
        update_clear_auto();if(update_auto_matches())return 1;
        update_active=U_GITHUB;update_percent=100;if(update_display_percent()!=70)return 1;
        update_active=U_INSTALL;update_percent=0;if(update_display_percent()!=70)return 1;
        update_parse("POCKETOS PHASE REBOOT");if(!update_handoff||strcmp(update_note,"Restarting into recovery"))return 1;
        update_handoff=0;
        update_active=U_NONE;update_confirm=U_INSTALL;
        tap(20,260);if(update_confirm||update_pid>=0)return 1;
        selected=2;
        if(!key(KEY_DOWN)||selected!=0)return 1;
        render();update_close();return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--test-radio")){
        char scan[]="bssid / frequency / signal level / flags / ssid\n00:11:22:33:44:55\t2412\t-50\t[WPA2-PSK-CCMP][ESS]\tHome\\x20WiFi\n00:11:22:33:44:56\t2412\t-70\t[ESS]\tGuest\n00:11:22:33:44:57\t2412\t-90\t[WPA2-PSK-CCMP][ESS]\tHome WiFi\n";
        radio_parse_wifi(scan);
        if(radio_count!=2||strcmp(radio_items[0].name,"Home WiFi")||radio_items[0].signal!=-50||radio_items[1].security!=0)return 1;
        if(radio_spawn(R_WIFI_SCAN,NULL)||radio_pid>=0)return 1;
        radio_allow=1;page=WIFIUI;radio_choose();if(!radio_keyboard||strcmp(radio_ssid,"Home WiFi"))return 1;
        radio_tap(10,101);if(strcmp(radio_key,"a"))return 1;
        radio_tap(190,65);if(!radio_show_key)return 1;
        radio_tap(2,300);if(radio_keyboard||radio_key[0]||radio_show_key)return 1;
        char devices[]="Device 00:11:22:33:44:55 Headphones\nDevice invalid device\n";
        radio_parse_bt(devices);if(radio_count!=1||strcmp(radio_items[0].name,"Headphones"))return 1;
        radio_close();return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--test-files")&&argv[2][0]=='/'){
        if(!fm_attach(argv[2])||!fm_scan()||!fm_enter("Documents"))return 1;
        if(fm_open_dir("../")>=0||fm_enter("link"))return 1;
        if(!fm_capture("example.txt",1)||!fm_up()||!fm_enter("Downloads"))return 1;
        if(fm_transfer("existing.txt",0)||!fm_transfer("example.txt",0))return 1;
        if(!fm_capture("example.txt",2)||fm_transfer("../bad",1)||!fm_transfer("renamed.txt",1))return 1;
        if(!fm_capture("renamed.txt",2)||!fm_delete())return 1;
        if(!fm_up()||!fm_enter("PocketOS-updates")||fm_capture("protected.txt",2)||fm_mkdir("bad"))return 1;
        if(!fm_up()||fm_capture("r1.upt",2)||!fm_capture("loose.txt",2)||!fm_enter("Documents"))return 1;
        if(!fm_transfer("loose.txt",1)||!fm_mkdir("New folder")||fm_mkdir("New folder")||fm_mkdir("../escape"))return 1;
        if(!fm_enter("New folder")||!fm_up()||strcmp(fm_items[fm_index].name,"New folder"))return 1;
        fm_select(-999);if(fm_index!=0)return 1;
        fm_select(999);if(fm_index!=fm_count-1)return 1;
        if(!fm_capture("large.bin",1))return 1;
        strcpy(fm_rename,"large copy.bin");
        if(!fm_begin_transfer(3))return 1;
        for(int i=0;i<1000&&fm_worker>=0;i++){(void)fm_poll_transfer();usleep(10000);}
        if(fm_worker>=0||strcmp(fm_message,"DONE"))return 1;
        fm_close();return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--test-books")&&argv[2][0]=='/'){
        book_dir=argv[2];book_scan();
        for(int i=0;i<book_count;i++)if(!strcmp(books[i],"example.txt")){book_index=i;break;}
        if(!book_count||strcmp(books[book_index],"example.txt")||!book_open())return 1;
        if(strcmp(book_text[0],"CHAPTER ONE")||book_eof)return 1;
        if(reader_measure("iiii",17,0,0)>=reader_measure("WWWW",17,0,0))return 1;
        render();if(canvas[0]!=RGB(35,43,29))return 1;
        tap(120,120);if(!book_menu)return 1;
        tap(200,300);if(book_menu||page!=READER)return 1;
        book_turn(1);if(book_pos<=0)return 1;
        book_turn(0);if(book_pos!=0||strcmp(book_text[0],"CHAPTER ONE"))return 1;
        book_allow_save=1;
        off_t positions[4]={0};
        for(int i=1;i<4;i++){book_turn(1);positions[i]=book_pos;if(positions[i]<=positions[i-1])return 1;}
        book_close();if(!book_open()||book_pos!=positions[3]||book_depth!=3)return 1;
        for(int i=2;i>=0;i--){book_turn(0);if(book_pos!=positions[i])return 1;}
        tap(120,120);if(!book_menu)return 1;
        for(int i=2;i<=4;i++){tap(120,70);if(book_size!=i||book_next<=book_pos)return 1;}
        tap(120,70);if(book_size!=1)return 1;
        tap(120,70);tap(200,300);book_close();if(!book_open()||book_size!=2)return 1;
        book_allow_save=0;book_close();
        char bookmark[600];if(!bookmark_path(bookmark))return 1;
        FILE *fixture=fopen(bookmark,"w");if(!fixture)return 1;
        fprintf(fixture,"POCKETBOOK1 %lld %lld %lld 0 1 0 3 0\n",(long long)book_bytes,book_mtime,(long long)positions[1]);
        if(fclose(fixture)||!book_open()||book_pos!=positions[1]||book_depth!=1)return 1;
        tap(120,120);tap(120,150);if(book_depth!=1||book_theme!=0)return 1;
        book_close();
        fixture=fopen(bookmark,"w");if(!fixture)return 1;
        fprintf(fixture,"POCKETBOOK2 %lld %lld %lld 1 0 3 0 2\n0\n0\n",(long long)book_bytes,book_mtime,(long long)positions[2]);
        if(fclose(fixture)||!book_open()||book_pos!=0||book_depth!=0)return 1;
        book_close();
        return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--test-backlight")&&argv[2][0]=='/') {
        if(write_backlight(argv[2],50))return 1;
        allow_backlight=1;
        if(write_backlight(argv[2],0)||write_backlight(argv[2],101))return 1;
        return !write_backlight(argv[2],50);
    }
    if(argc==3&&!strcmp(argv[1],"--test-volume-config")&&argv[2][0]=='/'){
        config_path=argv[2];volume=130;volume_extended=1;
        if(!save_preferences())return 1;
        load_preferences();return volume!=VOL_BOOT_MAX||volume_extended||volume_warning;
    }
    if(argc==3&&!strcmp(argv[1],"--test-config")&&argv[2][0]=='/') {
        config_path=argv[2];theme=2;clock24=readable=saver=1;timezone_choice=3;volume=37;output=2;
        /* First prove migration of the old installed config. */
        FILE *legacy=fopen(config_path,"w");if(!legacy)return 1;
        if(fputs("POCKETOS3 2 1 1 1 3 37 2\n",legacy)<0){fclose(legacy);return 1;}
        if(fclose(legacy))return 1;
        load_preferences();if(theme!=2||volume!=37||brightness_saved!=-1||music_shuffle||music_repeat)return 1;
        FILE *old=fopen(config_path,"w");if(!old)return 1;
        if(fputs("POCKETOS4 2 1 1 1 3 37 0 -1 0 0 0\n",old)<0){fclose(old);return 1;}
        if(fclose(old))return 1;
        load_preferences();if(output!=3)return 1;
        if(!save_preferences())return 1;
        output=0;load_preferences();if(output!=3)return 1;
        output=2;
        brightness_saved=42;music_shuffle=1;music_repeat=2;music_list=1;
        if(!save_preferences())return 1;
        theme=clock24=readable=saver=timezone_choice=volume=output=music_shuffle=music_repeat=music_list=0;brightness_saved=-1;load_preferences();
        return !(theme==2&&clock24&&readable&&saver&&timezone_choice==3&&volume==37&&output==2&&
                 brightness_saved==42&&music_shuffle==1&&music_repeat==2&&music_list==1);
    }
    if(argc==2&&!strcmp(argv[1],"--test-smart-sleep")){int rc=suspend_key_test();return rc?rc:smart_audio_test();}
    if(argc==4&&!strcmp(argv[1],"--preview")){
        int found=0;for(unsigned i=0;i<sizeof(titles)/sizeof(titles[0]);i++)if(!strcmp(argv[2],titles[i])){page=(enum page)i;found=1;}
        if(!found)return 2;
        struct tm sample={.tm_hour=10,.tm_min=42};
        format_clock(&sample,clock_label);
        battery=78;
        strcpy(date_label,"2026-11-15");
        if(page==POWER){battery=78;ram_total=65536;ram_available=32768;shell_rss=1024;}
        if(page==UPDATES){update_installed=11;update_current=12;update_previous=11;update_allow=1;}
        if(page==WIFIUI||page==BTUI){radio_mode=page==BTUI;radio_count=2;strcpy(radio_items[0].name,radio_mode?"Wireless Headphones":"Home Wi-Fi");strcpy(radio_items[1].name,radio_mode?"Portable Speaker":"Guest Network");strcpy(radio_items[0].address,"00:11:22:33:44:55");strcpy(radio_items[1].address,"00:11:22:33:44:56");radio_items[0].security=1;radio_items[0].signal=-48;radio_items[1].signal=-67;radio_message(radio_mode?"Choose a device":"Choose a network");}
        if(page==FILES){fm_preview=1;fm_count=4;strcpy(fm_items[0].name,"Books");fm_items[0].dir=1;strcpy(fm_items[1].name,"Music");fm_items[1].dir=1;strcpy(fm_items[2].name,"Documents");fm_items[2].dir=1;strcpy(fm_items[3].name,"Read me.txt");fm_items[3].bytes=2048;}
        if(page==BOOKS){book_count=3;strcpy(books[0],"Demo.epub");strcpy(books[1],"Notes.txt");strcpy(books[2],"Loan.lcpl");}
        if(page==READER){strcpy(book_title,"The Quiet Woods");const char *demo[]={"Chapter One","The town appeared just after","dusk, a scatter of warm lights","against the hillside. After a","long day on the road, it felt","almost unreal, quiet and","strangely familiar.","","He paused at the edge of the","square, listening to a distant","bell. Beyond the rooftops,","a train sighed into the night."};for(unsigned i=0;i<sizeof(demo)/sizeof(demo[0]);i++)snprintf(book_text[i],65,"%s",demo[i]);book_bytes=99000;book_pos=11500;book_next=12000;book_page_span=500;battery=78;strcpy(clock_label,"22:08");}
        if(page==MUSIC||page==NOWPLAYING){music_pid=getpid();music_pause=1;music_count=3;strcpy(music_files[0],"Track 01.flac");strcpy(music_files[1],"Track 02.mp3");strcpy(music_files[2],"Track 03.wav");strcpy(track_title,"Track 01");strcpy(track_artist,"Demo Artist");strcpy(track_album,"Demo Album");music_pos=45;music_length=210;}
        render();
        return ppm(argv[3]);
    }
    if(argc>=5&&!strcmp(argv[1],"--config")&&argv[2][0]=='/') {config_path=argv[2];argc-=2;argv+=2;}
    while(argc>=4){
        if(!strcmp(argv[1],"--allow-backlight"))allow_backlight=1;
        else if(!strcmp(argv[1],"--allow-audio"))allow_audio=1;
        else if(!strcmp(argv[1],"--allow-bookmarks"))book_allow_save=1;
        else if(!strcmp(argv[1],"--allow-book-preparation"))book_allow_prepare=1;
        else if(!strcmp(argv[1],"--allow-file-writes"))fm_allow=1;
        else if(!strcmp(argv[1],"--allow-radio"))radio_allow=1;
        else if(!strcmp(argv[1],"--allow-updates"))update_allow=1;
        else if(!strcmp(argv[1],"--boot-splash"))boot_splash=1;
        else if(!strcmp(argv[1],"--unblank-display"))unblank_display=1;
        else if(!strcmp(argv[1],"--exclusive-input"))exclusive_input=1;
        else if(!strcmp(argv[1],"--trace-touch"))trace_touch=1;
        else if(!strcmp(argv[1],"--allow-smart-sleep"))smart_allow=1;
        else if(!strcmp(argv[1],"--allow-idle-dim"))power_allow=1;
        else if(!strcmp(argv[1],"--allow-screen-off")){power_allow=1;power_blank=1;}
        else if(!strcmp(argv[1],"--allow-cpu-policy"))power_cpu_allow=1;
        else break;
        argc--;argv++;
    }
    const char *tz=getenv("TZ");if(tz)original_tz=strdup(tz);
    if(argc==3&&!strcmp(argv[1],"--fb")&&!strncmp(argv[2],"/dev/input/event",16)) {
        const char *n=argv[2]+16;
        if(*n&&strspn(n,"0123456789")==strlen(n))return native(argv[2]);
    }
    fputs("Usage: pocketos-shell --preview HOME|SETTINGS|SYSTEM output.ppm | --self-test | --fb /dev/input/eventN\n",stderr);return 2;
}
