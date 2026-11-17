/* Classic controls draw directly into the existing shell canvas. */
static const uint16_t cp_ink=RGB(26,31,38),cp_soft=RGB(94,104,117),cp_white=RGB(255,255,255);
static void cp_point(int x,int y,uint16_t c){if(x>=0&&x<CW&&y>=0&&y<CH)canvas[y*CW+x]=c;}
static void cp_box(int x,int y,int w,int h,uint16_t c){for(int j=0;j<h;j++)for(int i=0;i<w;i++)cp_point(x+i,y+j,c);}
static uint16_t cp_mix(uint16_t a,uint16_t b,unsigned n){
    unsigned r=(((a>>11)&31)*(255-n)+((b>>11)&31)*n)/255;
    unsigned g=(((a>>5)&63)*(255-n)+((b>>5)&63)*n)/255;
    unsigned v=((a&31)*(255-n)+(b&31)*n)/255;return (uint16_t)((r<<11)|(g<<5)|v);
}
static void cp_gradient(int x,int y,int w,int h,uint16_t a,uint16_t b){for(int j=0;j<h;j++)cp_box(x,y+j,w,1,cp_mix(a,b,(unsigned)(255*j/(h-1))));}
static int cp_width(const char *s,int style){int n=0;for(;*s;s++){unsigned c=(unsigned char)*s;if(c<32||c>126)c='?';n+=ui_advance[style][c-32];}return n;}
static void cp_text(int x,int y,const char *s,int style,uint16_t color,int limit){
    for(;*s;s++){
        unsigned c=(unsigned char)*s;if(c<32||c>126)c='?';
        unsigned glyph=(unsigned)style*95+c-32,at=ui_offsets[glyph],end=ui_offsets[glyph+1],pos=0;
        if(x+ui_advance[style][c-32]>limit)break;
        while(at<end){pos+=ui_data[at++];unsigned count=ui_data[at++];
            for(unsigned k=0;k<count;k++,pos++){unsigned value=ui_data[at++];
                for(unsigned half=0;half<2;half++){
                    unsigned p=pos*2+half,a=((value>>(half?0:4))&15u)*17u;
                    int xx=x+(int)(p%UI_ATLAS_WIDTH),yy=y+(int)(p/UI_ATLAS_WIDTH);
                    if(a&&xx>=0&&xx<CW&&yy>=0&&yy<CH)cp_point(xx,yy,cp_mix(canvas[yy*CW+xx],color,a));
                }
            }
        }
        x+=ui_advance[style][c-32];
    }
}
static void cp_centered(int mid,int y,const char *s,int style,uint16_t c){cp_text(mid-cp_width(s,style)/2,y,s,style,c,460);}
static void cp_triangle(int x,int y,int size,int right,uint16_t c){for(int i=0;i<size;i++)cp_box(x+(right?i:size-1-i),y+i,1,2*(size-i)-1,c);}
static void cp_wheel(void){
    for(int y=-124;y<=124;y++)for(int x=-124;x<=124;x++){
        int r=x*x+y*y;
        if(r<=124*124)cp_point(240+x,661+y,r>122*122?RGB(157,165,176):r<43*43?cp_mix(RGB(219,223,229),cp_white,(unsigned)((y+43)*180/86)):RGB(246,247,249));
    }
    cp_centered(240,558,"MENU",0,cp_soft);
    cp_triangle(135,649,12,0,cp_soft);cp_triangle(148,649,12,0,cp_soft);cp_box(131,649,3,23,cp_soft);
    cp_triangle(321,649,12,1,cp_soft);cp_triangle(334,649,12,1,cp_soft);cp_box(348,649,3,23,cp_soft);
    cp_triangle(215,744,12,1,cp_soft);cp_box(245,744,4,23,cp_soft);cp_box(255,744,4,23,cp_soft);
}
static void cp_shell(const char *title){
    cp_gradient(0,0,CW,CH,RGB(226,230,236),RGB(193,201,212));
    cp_box(14,13,452,510,RGB(153,164,180));cp_box(19,18,442,500,cp_white);
    cp_gradient(20,19,440,57,RGB(253,254,255),RGB(204,213,225));cp_box(20,76,440,2,RGB(166,179,195));
    cp_centered(240,29,title,1,cp_ink);
    cp_box(411,36,31,15,RGB(112,126,146));cp_box(414,39,24,9,RGB(79,149,91));cp_box(442,40,3,7,RGB(112,126,146));
    cp_triangle(35,37,8,1,cp_soft);
    cp_wheel();
}
static void cp_selected_row(int x,int y,int w,const char *label,int active){
    if(active){cp_gradient(x,y,w,51,RGB(66,131,222),RGB(20,71,154));cp_box(x,y,w,1,RGB(118,163,232));}
    cp_text(x+13,y+9,label,1,active?cp_white:cp_ink,x+w-27);
    cp_text(x+w-22,y+10,">",1,active?cp_white:cp_soft,x+w);
}
static void cp_album(int x,int y,int size){
    cp_box(x+5,y+6,size,size,RGB(184,194,207));
    cp_gradient(x,y,size,size,RGB(29,57,99),RGB(88,129,162));
    for(int j=0;j<size;j++)for(int i=0;i<size;i++){
        int dx=i-size/2,dy=j-size/2,r=dx*dx+dy*dy;
        if(r<(size*size)/9&&r>(size*size)/20)cp_point(x+i,y+j,RGB(232,184,110));
    }
    cp_box(x+size/5,y+size*3/4,size*3/5,2,RGB(201,218,233));
}
