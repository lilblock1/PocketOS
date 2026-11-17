/* Functional Classic shell. Rendering reuses canvas; browsing never changes
 * the active decoder until the owner selects a track. Detailed settings and
 * reading retain the existing full-screen controls. */
#include "pocketos-classic-draw.h"
static int classic_ui,classic_music_cursor,classic_music_top,classic_books_top;
static int classic_seek_mode,classic_sector=-1,classic_accum,classic_moved;
static int classic_page(void){
    return classic_ui&&(page==HOME||page==MUSIC||page==BOOKS||page==SETTINGS||page==NOWPLAYING);
}
static int classic_window(int index,int count,int *top){
    if(index<0)index=0;
    if(index>=count)index=count?count-1:0;
    if(index<*top)*top=index;
    if(index>=*top+7)*top=index-6;
    if(*top>count-7)*top=count>7?count-7:0;
    return index;
}
static void classic_status(const char *label){
    cp_box(20,478,440,39,RGB(239,243,248));
    cp_text(32,485,label,0,cp_soft,450);
}
static void classic_render(void){
    cp_shell(page==HOME?"PocketOS":page==NOWPLAYING?"Now Playing":page_label(page));
    /* Replace the concept's sample battery with the current device status. */
    cp_box(411,36,31,15,RGB(112,126,146));cp_box(414,39,24,9,RGB(239,243,248));
    if(battery>=0)cp_box(414,39,24*(battery>100?100:battery)/100,9,RGB(79,149,91));
    if(music_pid>=0&&!music_pause)cp_triangle(35,37,8,1,cp_soft);
    else cp_box(33,34,20,23,RGB(238,242,248));
    if(page==HOME){
        static const char *const labels[]={"Music","Books","Files","Settings","Controls","About"};
        static const char *const hints[]={"Your library","Read a book","On microSD","Your player","Quick access","Device info"};
        cp_gradient(276,78,184,438,RGB(244,247,251),RGB(224,232,242));
        for(int i=0;i<6;i++)cp_selected_row(27,104+i*56,239,labels[i],selected==i);
        cp_box(275,78,1,438,RGB(194,205,222));cp_album(291,156,143);
        cp_centered(367,327,selected<6?labels[selected]:"Playing",1,cp_ink);
        cp_centered(367,365,selected<6?hints[selected]:"Your music",0,cp_soft);
        if(selected==6)cp_gradient(20,478,440,39,RGB(66,131,222),RGB(20,71,154));
        else cp_box(20,478,440,39,RGB(239,243,248));
        cp_centered(240,482,"Now Playing  >",0,selected==6?cp_white:cp_ink);
    }else if(page==SETTINGS){
        static const char *const labels[]={"Screen","Sound","Wi-Fi & Bluetooth","Battery","Storage","Updates","Appearance"};
        for(int i=0;i<7;i++)cp_selected_row(27,91+i*56,426,labels[i],selected==i);
    }else if(page==MUSIC||page==BOOKS){
        int count=page==MUSIC?music_count:book_count;
        int *top=page==MUSIC?&classic_music_top:&classic_books_top;
        int index=page==MUSIC?classic_music_cursor:book_index;
        index=classic_window(index,count,top);
        if(page==MUSIC)classic_music_cursor=index;else book_index=index;
        for(int i=0;i<7&&*top+i<count;i++){
            int item=*top+i;char label[100];
            if(page==BOOKS)book_label(item,label,sizeof(label));
            else{const char *name=strrchr(music_files[item],'/');snprintf(label,sizeof(label),"%.99s",name?name+1:music_files[item]);}
            cp_selected_row(27,91+i*54,426,label,item==index);
        }
        char status[100];
        if(!count)snprintf(status,sizeof(status),"Add %s to microSD / %s",page==MUSIC?"music":"books",page==MUSIC?"Music":"Books");
        else if(page==BOOKS&&book_status[0])snprintf(status,sizeof(status),"%s",book_status);
        else if(page==MUSIC&&music_error)snprintf(status,sizeof(status),"%s",music_error_text(music_error));
        else snprintf(status,sizeof(status),"%d of %d%s",count?index+1:0,count,page==MUSIC&&music_pid>=0?(music_pause?"  -  Paused":"  -  Playing"):"");
        classic_status(status);
    }else{
        if(cover_ok){
            for(int y=0;y<185;y++)for(int x=0;x<185;x++){
                size_t at=(size_t)((y*64/185)*64+x*64/185)*3;
                cp_point(39+x,123+y,RGB(cover[at],cover[at+1],cover[at+2]));
            }
        }else cp_album(39,123,185);
        cp_text(247,141,track_title[0]?track_title:"Choose a track",1,cp_ink,449);
        cp_text(248,187,track_artist[0]?track_artist:"Music library",0,cp_soft,449);
        cp_text(248,221,track_album,0,cp_soft,449);
        cp_box(40,378,400,13,RGB(211,218,228));
        if(music_length)cp_gradient(40,378,(int)(400*(uint64_t)(music_pos>music_length?music_length:music_pos)/music_length),13,RGB(66,131,222),RGB(20,71,154));
        char elapsed[20],duration[20],status[100];
        snprintf(elapsed,sizeof(elapsed),"%u:%02u",music_pos/60,music_pos%60);
        if(music_length)snprintf(duration,sizeof(duration),"%u:%02u",music_length/60,music_length%60);else strcpy(duration,"--:--");
        cp_text(40,406,elapsed,0,cp_soft,200);cp_text(440-cp_width(duration,0),406,duration,0,cp_soft,445);
        if(music_error)snprintf(status,sizeof(status),"%s",music_error_text(music_error));
        else snprintf(status,sizeof(status),"%s  -  %s %d%s",music_pid<0?"Stopped":music_pause?"Paused":"Playing",classic_seek_mode?"Seek":"Volume",classic_seek_mode&&music_length?(int)(100*(uint64_t)music_pos/music_length):volume,"%");
        classic_status(status);
    }
}
/* Integer 32-sector wheel, clockwise from the right edge. No libm or cache. */
static int classic_angle(int x,int y){
    int dx=x-240,dy=y-661,r=dx*dx+dy*dy;
    if(r<48*48||r>126*126)return -1;
    int ax=abs(dx),ay=abs(dy),q=ax>=ay?(ax?ay*4/ax:0):8-ax*4/ay;
    return (dy>=0?(dx>=0?q:16-q):(dx<0?16+q:32-q))%32;
}
static void classic_reset_gesture(void){classic_sector=-1;classic_accum=classic_moved=0;}
static void classic_step(int step);
static int classic_motion(int x,int y){
    if(!classic_page())return 0;
    int angle=classic_angle(x,y);if(angle<0){classic_sector=-1;return 0;}
    if(classic_sector<0){classic_sector=angle;return 0;}
    int delta=angle-classic_sector;classic_sector=angle;
    if(delta>16)delta-=32;else if(delta<-16)delta+=32;
    if(abs(delta)>6){classic_accum=0;return 0;}
    classic_accum+=delta;int changed=0;
    while(classic_accum>=2||classic_accum<=-2){
        int step=classic_accum>0?1:-1;classic_accum-=step*2;
        classic_step(step);classic_moved=changed=1;
    }
    return changed;
}
