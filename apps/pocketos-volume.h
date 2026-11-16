#ifndef POCKETOS_VOLUME_H
#define POCKETOS_VOLUME_H
/* UI 100 = tested 15% squared PCM gain. Extended 130 = 19.5%, never amplify. */
enum { VOL_NORMAL=100, VOL_MAX=130, VOL_BOOT_MAX=50, VOL_ENABLE=203, VOL_DISABLE=204 };
static inline int vol_clamp(int v,int extended){int max=extended?VOL_MAX:VOL_NORMAL;return v<0?0:v>max?max:v;}
static inline void vol_command(unsigned char cmd,int *volume,int *extended){
    if(cmd==VOL_ENABLE)*extended=1;
    else if(cmd==VOL_DISABLE){*extended=0;*volume=vol_clamp(*volume,0);}
    else if(cmd<=VOL_MAX)*volume=vol_clamp(cmd,*extended);
}
#endif
