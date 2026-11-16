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

#define W 240
#define H 320
#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
static uint16_t canvas[W*H];
static const uint16_t bg=RGB(13,19,24), card=RGB(25,35,43), edge=RGB(47,62,70),
    fg=RGB(239,246,246), muted=RGB(151,171,179), mint=RGB(109,226,182);
enum page { HOME, MUSIC, BOOKS, GAMES, FILES, SETTINGS, DIAGNOSTICS, DISPLAY, SOUND, RADIO, POWER, STORAGE, UPDATES };
static enum page page=HOME;
static int selected=0, battery=-1;
static char clock_label[16]="--:--";
static const char *titles[]={"HOME","MUSIC","BOOKS","GAMES","FILES","SETTINGS","SYSTEM","DISPLAY","SOUND","RADIOS","BATTERY","STORAGE","UPDATES"};
static const enum page home_items[]={MUSIC,BOOKS,GAMES,FILES,SETTINGS,DIAGNOSTICS};
static const enum page setting_items[]={DISPLAY,SOUND,RADIO,POWER,STORAGE,UPDATES};
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
    int h=t->tm_hour%12;
    snprintf(out,16,"%d:%02d %s",h?h:12,t->tm_min,t->tm_hour<12?"AM":"PM");
}
static int update_clock(int *changed) {
    time_t now=time(NULL);
    struct tm t;
    char next[16];
    int valid=now!=(time_t)-1&&localtime_r(&now,&t)!=NULL&&t.tm_year>=120;
    format_clock(valid?&t:NULL,next);
    *changed=strcmp(next,clock_label)!=0;
    if(*changed)memcpy(clock_label,next,strlen(next)+1);
    /* Reuse the existing <=30 second wait and align it with the next minute. */
    if(valid&&t.tm_sec>=0&&t.tm_sec<60) {
        int ms=(60-t.tm_sec)*1000;
        return ms<30000?ms:30000;
    }
    return 30000;
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
    int y=80+i*33;
    rounded(12,y,216,28,i==selected?edge:card);
    if(i==selected)rect(12,y+5,2,18,mint);
    text_at(21,y+7,label,2,fg);text_at(216,y+11,"-",1,mint);
}
static void render(void) {
    rect(0,0,W,H,bg);statusbar();
    text_at(12,37,titles[page],3,fg);
    if(page==HOME) {
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
        for(int i=0;i<6;i++) row(i,titles[setting_items[i]]);
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
static void back(void) { page=(page>=DISPLAY?SETTINGS:HOME); selected=0; }
static void activate(int i) {
    if(i<0||i>=6)return;
    if(page==HOME)page=home_items[i];else if(page==SETTINGS)page=setting_items[i];else return;
    selected=0;
}
static void tap(int x,int y) {
    if(y>=287&&page!=HOME){back();return;}
    if(x<12||x>=228||y<80)return;
    if(page==HOME&&y<278) {
        int a=x-12,b=y-80;
        if(a%110<106&&b%66<60)activate((b/66)*2+a/110);
    } else if(page==SETTINGS&&y<278&&(y-80)%33<28) activate((y-80)/33);
}
static int key(unsigned code) {
    int prev=selected;enum page prior=page;
    if(code==KEY_ESC||code==KEY_BACK)back();
    else if(code==KEY_ENTER||code==KEY_OK||code==KEY_SELECT)activate(selected);
    else if(page==HOME||page==SETTINGS) {
        if(code==KEY_DOWN)selected=(selected+(page==HOME?2:1))%6;
        else if(code==KEY_UP)selected=(selected+(page==HOME?4:5))%6;
        else if(code==KEY_RIGHT)selected=(selected+1)%6;
        else if(code==KEY_LEFT)selected=(selected+5)%6;
    }
    return prev!=selected||prior!=page;
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
    int clock_changed=0;
    (void)update_clock(&clock_changed);
    battery=sample_battery();render();present(&b);
    for(;;){
        int timeout=update_clock(&clock_changed);
        if(clock_changed){render();present(&b);}
        struct pollfd p={.fd=fd,.events=POLLIN};int rc=poll(&p,1,timeout);
        if(rc<0){if(errno==EINTR)continue;break;}
        if(!rc){int n=sample_battery();if(n!=battery){battery=n;render();present(&b);}continue;}
        if(!(p.revents&POLLIN)||(p.revents&(POLLERR|POLLHUP|POLLNVAL)))break;
        struct input_event e[16];ssize_t got=read(fd,e,sizeof(e));
        if(got<0){if(errno==EAGAIN||errno==EINTR)continue;break;}if(!got)break;
        int changed=0;
        for(size_t i=0;i<(size_t)got/sizeof(*e);i++){
            struct input_event v=e[i];
            if(v.type==EV_SYN&&v.code==SYN_DROPPED){dropped=1;pending=down=have_x=have_y=0;continue;}
            if(dropped){if(v.type==EV_SYN&&v.code==SYN_REPORT)dropped=0;continue;}
            if(v.type==EV_ABS&&v.code==ABS_MT_SLOT){slot=v.value;continue;}
            if(touch&&v.type==EV_ABS&&(!mt||slot==0)){
                if(v.code==(mt?ABS_MT_POSITION_X:ABS_X)){rx=v.value;have_x=1;}
                else if(v.code==(mt?ABS_MT_POSITION_Y:ABS_Y)){ry=v.value;have_y=1;}
                else if(mt&&v.code==ABS_MT_TRACKING_ID){if(v.value>=0){down=1;have_x=have_y=0;}else if(down){pending=1;down=0;}}
            }
            if(touch&&!mt&&v.type==EV_KEY&&v.code==BTN_TOUCH){if(v.value==1){down=1;}else if(!v.value&&down){pending=1;down=0;}}
            if(v.type==EV_KEY&&v.value==1&&v.code!=BTN_TOUCH)changed|=key(v.code);
            if(v.type==EV_SYN&&v.code==SYN_REPORT&&pending){
                if(have_x&&have_y){
                    int64_t px=((int64_t)rx-ax.minimum)*(b.v.xres-1)/((int64_t)ax.maximum-ax.minimum);
                    int64_t py=((int64_t)ry-ay.minimum)*(b.v.yres-1)/((int64_t)ay.maximum-ay.minimum);
                    if(px>=b.ox&&px<(int64_t)b.ox+b.vw&&py>=b.oy&&py<(int64_t)b.oy+b.vh){enum page old=page;tap((int)((px-b.ox)*W/b.vw),(int)((py-b.oy)*H/b.vh));changed|=old!=page;}
                }
                pending=0;have_x=have_y=0;
            }
        }
        if(changed){render();present(&b);}
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
    render();return 0;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS shell v1");return 0;}
    if(argc==2&&!strcmp(argv[1],"--self-test"))return selftest();
    if(argc==4&&!strcmp(argv[1],"--preview")){
        int found=0;for(unsigned i=0;i<sizeof(titles)/sizeof(titles[0]);i++)if(!strcmp(argv[2],titles[i])){page=(enum page)i;found=1;}
        if(!found)return 2;
        struct tm sample={.tm_hour=10,.tm_min=42};
        format_clock(&sample,clock_label);
        render();
        return ppm(argv[3]);
    }
    if(argc==3&&!strcmp(argv[1],"--fb")&&!strncmp(argv[2],"/dev/input/event",16)) {
        const char *n=argv[2]+16;
        if(*n&&strspn(n,"0123456789")==strlen(n))return native(argv[2]);
    }
    fputs("Usage: pocketos-shell --preview HOME|SETTINGS|SYSTEM output.ppm | --self-test | --fb /dev/input/eventN\n",stderr);return 2;
}
