/* Host-only 480x800 visual proposal. No device access or update operation.
 * Reuses PocketOS's native glyphs and one RGB565 canvas. Interaction is a
 * design proposal until approved and integrated into the actual shell.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pocketos-reader-font.h"
#define W 480
#define H 800
#define C(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
static uint16_t pixels[W*H];
static const uint16_t ink=C(26,31,38),soft=C(94,104,117),blue=C(30,91,185),white=C(255,255,255);
static void point(int x,int y,uint16_t c){if(x>=0&&x<W&&y>=0&&y<H)pixels[y*W+x]=c;}
static void box(int x,int y,int w,int h,uint16_t c){for(int j=0;j<h;j++)for(int i=0;i<w;i++)point(x+i,y+j,c);}
static uint16_t mix(uint16_t a,uint16_t b,unsigned n){
    unsigned r=(((a>>11)&31)*(255-n)+((b>>11)&31)*n)/255;
    unsigned g=(((a>>5)&63)*(255-n)+((b>>5)&63)*n)/255;
    unsigned v=((a&31)*(255-n)+(b&31)*n)/255;return (uint16_t)((r<<11)|(g<<5)|v);
}
static void gradient(int x,int y,int w,int h,uint16_t a,uint16_t b){for(int j=0;j<h;j++)box(x,y+j,w,1,mix(a,b,(unsigned)(255*j/(h-1))));}
static int width(const char *s,int style){int n=0;for(;*s;s++){unsigned c=(unsigned char)*s;if(c<32||c>126)c='?';n+=ui_advance[style][c-32];}return n;}
static void text(int x,int y,const char *s,int style,uint16_t color,int limit){
    for(;*s;s++){
        unsigned c=(unsigned char)*s;if(c<32||c>126)c='?';
        unsigned glyph=(unsigned)style*95+c-32,at=ui_offsets[glyph],end=ui_offsets[glyph+1],pos=0;
        if(x+ui_advance[style][c-32]>limit)break;
        while(at<end){pos+=ui_data[at++];unsigned count=ui_data[at++];
            for(unsigned k=0;k<count;k++,pos++){unsigned value=ui_data[at++];
                for(unsigned half=0;half<2;half++){
                    unsigned p=pos*2+half,a=((value>>(half?0:4))&15u)*17u;
                    int xx=x+(int)(p%UI_ATLAS_WIDTH),yy=y+(int)(p/UI_ATLAS_WIDTH);
                    if(a&&xx>=0&&xx<W&&yy>=0&&yy<H)point(xx,yy,mix(pixels[yy*W+xx],color,a));
                }
            }
        }
        x+=ui_advance[style][c-32];
    }
}
static void centered(int mid,int y,const char *s,int style,uint16_t c){text(mid-width(s,style)/2,y,s,style,c,460);}
static void triangle(int x,int y,int size,int right,uint16_t c){for(int i=0;i<size;i++)box(x+(right?i:size-1-i),y+i,1,2*(size-i)-1,c);}
static void wheel(void){
    for(int y=-124;y<=124;y++)for(int x=-124;x<=124;x++){
        int r=x*x+y*y;
        if(r<=124*124)point(240+x,661+y,r>122*122?C(157,165,176):r<43*43?mix(C(219,223,229),white,(unsigned)((y+43)*180/86)):C(246,247,249));
    }
    centered(240,558,"MENU",0,soft);
    triangle(135,649,12,0,soft);triangle(148,649,12,0,soft);box(131,649,3,23,soft);
    triangle(321,649,12,1,soft);triangle(334,649,12,1,soft);box(348,649,3,23,soft);
    triangle(215,744,12,1,soft);box(245,744,4,23,soft);box(255,744,4,23,soft);
}
static void shell(const char *title){
    gradient(0,0,W,H,C(226,230,236),C(193,201,212));
    box(14,13,452,510,C(153,164,180));box(19,18,442,500,white);
    gradient(20,19,440,57,C(253,254,255),C(204,213,225));box(20,76,440,2,C(166,179,195));
    centered(240,29,title,1,ink);
    box(411,36,31,15,C(112,126,146));box(414,39,24,9,C(79,149,91));box(442,40,3,7,C(112,126,146));
    triangle(35,37,8,1,soft);
    wheel();
}
static void selected_row(int x,int y,int w,const char *label,int active){
    if(active){gradient(x,y,w,51,C(66,131,222),C(20,71,154));box(x,y,w,1,C(118,163,232));}
    text(x+13,y+9,label,1,active?white:ink,x+w-27);
    text(x+w-22,y+10,">",1,active?white:soft,x+w);
}
static void album(int x,int y,int size){
    box(x+5,y+6,size,size,C(184,194,207));
    gradient(x,y,size,size,C(29,57,99),C(88,129,162));
    for(int j=0;j<size;j++)for(int i=0;i<size;i++){
        int dx=i-size/2,dy=j-size/2,r=dx*dx+dy*dy;
        if(r<(size*size)/9&&r>(size*size)/20)point(x+i,y+j,C(232,184,110));
    }
    box(x+size/5,y+size*3/4,size*3/5,2,C(201,218,233));
}
static void home(void){
    shell("PocketOS");gradient(276,78,184,438,C(244,247,251),C(224,232,242));
    static const char *const labels[]={"Music","Books","Files","Settings","Controls","About"};
    for(int i=0;i<6;i++)selected_row(27,104+i*56,239,labels[i],i==0);
    box(275,78,1,438,C(194,205,222));
    box(302,145,137,137,C(162,182,205));album(291,156,143);
    centered(367,327,"Music",1,ink);centered(367,365,"Your library",0,soft);
    box(20,478,440,1,C(201,212,226));centered(240,482,"Now Playing  >",0,ink);
}
static void list(const char *title,int settings){
    shell(title);
    static const char *const prefs[]={"Screen","Sound","Wi-Fi & Bluetooth","Battery","Storage","Updates","Appearance"};
    static const char *const songs[]={"Track 01","Track 02","Track 03","Track 04","Track 05","Track 06","Track 07"};
    for(int i=0;i<7;i++)selected_row(27,91+i*56,426,settings?prefs[i]:songs[i],i==0);
}
static void player(void){
    shell("Now Playing");album(39,123,185);
    text(247,141,"Track 01",2,ink,449);text(248,187,"Demo Artist",0,soft,449);text(248,221,"Demo Album",0,soft,449);
    box(40,378,400,13,C(211,218,228));gradient(40,378,96,13,C(66,131,222),C(20,71,154));
    text(40,406,"0:45",0,soft,200);text(387,406,"3:30",0,soft,445);
    centered(240,471,"1 of 7  -  Paused",0,soft);
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    if(!strcmp(argv[1],"home"))home();else if(!strcmp(argv[1],"music"))list("Music",0);
    else if(!strcmp(argv[1],"settings"))list("Settings",1);else if(!strcmp(argv[1],"player"))player();else return 2;
    FILE *f=fopen(argv[2],"wb");if(!f)return 1;fprintf(f,"P6\n%d %d\n255\n",W,H);
    for(unsigned i=0;i<W*H;i++){uint16_t c=pixels[i];unsigned char b[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};if(fwrite(b,1,3,f)!=3){fclose(f);return 1;}}
    return fclose(f)!=0;
}
