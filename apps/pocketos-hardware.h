/* Shared read-only R1 discovery. Root prefix is for host fixtures only. */
#ifndef POCKETOS_HARDWARE_H
#define POCKETOS_HARDWARE_H
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define HW_PATH 2048
struct hw_battery { long percent; int charging, ambiguous; const char *source; };
static int hw_read(const char *dir,const char *attr,char *value,size_t cap){
    char path[HW_PATH];int n=snprintf(path,sizeof(path),"%s/%s",dir,attr);
    if(n<0||(size_t)n>=sizeof(path))return 0;
    FILE *f=fopen(path,"r");if(!f)return 0;
    int ok=fgets(value,(int)cap,f)!=NULL;fclose(f);
    if(!ok)return 0;
    size_t len=strcspn(value,"\r\n");if(len==cap-1)return 0;
    value[len]=0;return len>0;
}
static long hw_number(const char *dir,const char *attr,long low,long high){
    char value[64],*end;if(!hw_read(dir,attr,value,sizeof(value)))return -1;
    errno=0;long n=strtol(value,&end,10);
    return errno||!value[0]||*end||n<low||n>high?-1:n;
}
static int hw_driver(const char *dir,const char *name){
    char path[HW_PATH],link[HW_PATH];int n=snprintf(path,sizeof(path),"%s/device/driver",dir);
    if(n<0||(size_t)n>=sizeof(path))return 0;
    ssize_t len=readlink(path,link,sizeof(link)-1);if(len<0||(size_t)len>=sizeof(link)-1)return 0;
    link[len]=0;const char *last=strrchr(link,'/');return !strcmp(last?last+1:link,name);
}
static struct hw_battery hw_battery_read(const char *root){
    struct hw_battery result={-1,-1,0,"unknown"};
    char pattern[HW_PATH],chosen[HW_PATH]="",cw[HW_PATH]="",value[128];glob_t g={0};
    int n=snprintf(pattern,sizeof(pattern),"%s/sys/class/power_supply/*",root);
    if(n<0||(size_t)n>=sizeof(pattern))return result;
    int count=0,cws=0,pmics=0;
    if(glob(pattern,0,NULL,&g)==0)for(size_t i=0;i<g.gl_pathc;i++){
        const char *p=g.gl_pathv[i];if(strlen(p)>=sizeof(chosen))continue;
        if(!hw_read(p,"type",value,sizeof(value))||strcmp(value,"Battery"))continue;
        count++;strcpy(chosen,p);
        if(hw_driver(p,"cw201x")){cws++;strcpy(cw,p);}
        if(hw_driver(p,"axp2101-power-supply"))pmics++;
    }
    globfree(&g);
    /* The actual R1 exposes the CW fuel gauge and PMIC as two batteries.
     * Prefer CW only for this observed pair, never by capacity value or name. */
    if(count==2&&cws==1&&pmics==1){strcpy(chosen,cw);result.source="cw201x";}
    else if(count==1)result.source="single";
    else {result.ambiguous=count>1;return result;}
    if(hw_number(chosen,"present",0,1)==0)return result;
    if(!strcmp(result.source,"cw201x")&&hw_number(chosen,"voltage_now",1000000,6000000)<0)return result;
    result.percent=hw_number(chosen,"capacity",0,100);
    if(hw_read(chosen,"status",value,sizeof(value))){
        if(!strcmp(value,"Discharging"))result.charging=0;
        else if(!strcmp(value,"Charging")||!strcmp(value,"Full"))result.charging=1;
    }
    return result;
}
static int hw_mmc_partition(const char *source,char *disk,size_t cap,unsigned *partition){
    const char *prefix="/dev/mmcblk";size_t len=strlen(prefix);
    if(strncmp(source,prefix,len))return 0;
    const char *p=source+len,*first=p;unsigned d=0,part=0;
    while(*p>='0'&&*p<='9'&&p-first<3)d=d*10+(unsigned)(*p++-'0');
    if(p==first||*p++!='p')return 0;
    first=p;while(*p>='0'&&*p<='9'&&p-first<3)part=part*10+(unsigned)(*p++-'0');
    if(p==first||*p||!part||d>255||part>255)return 0;
    int n=snprintf(disk,cap,"mmcblk%u",d);if(n<0||(size_t)n>=cap)return 0;
    *partition=part;return 1;
}
static int hw_sd_mounted(const char *root){
    char path[HW_PATH],line[4096],source[256],mount[256],type[64],disk[32];
    int n=snprintf(path,sizeof(path),"%s/proc/mounts",root);if(n<0||(size_t)n>=sizeof(path))return 0;
    FILE *f=fopen(path,"r");if(!f)return 0;int seen=0,valid=0;
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')&&!feof(f)){valid=0;break;}
        if(sscanf(line,"%255s %255s %63s",source,mount,type)!=3||strcmp(mount,"/mnt/sd_0"))continue;
        seen++;unsigned part=0;
        if(strcmp(type,"vfat")&&strcmp(type,"exfat")&&strcmp(type,"ntfs")&&strcmp(type,"ntfs3")&&strcmp(type,"fuseblk"))continue;
        if(!hw_mmc_partition(source,disk,sizeof(disk),&part))continue;
        n=snprintf(path,sizeof(path),"%s/sys/class/block/%sp%u",root,disk,part);
        if(n<0||(size_t)n>=sizeof(path)||hw_number(path,"partition",1,255)!=(long)part)continue;
        n=snprintf(path,sizeof(path),"%s/sys/block/%s",root,disk);
        if(n<0||(size_t)n>=sizeof(path)||hw_number(path,"size",1,2147483647L)<0)continue;
        valid=1;
    }
    fclose(f);return seen==1&&valid;
}
#endif
