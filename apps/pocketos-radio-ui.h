/* On-demand UI radio runner. No persistent service or shell interpolation. */
#ifndef POCKETOS_RADIO_UI_H
#define POCKETOS_RADIO_UI_H
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define RADIO_CAP 32
#define RADIO_OUT 16384
enum radio_job {R_NONE,R_WIFI_SCAN,R_WIFI_STATUS,R_WIFI_JOIN,R_WIFI_DISCONNECT,R_BT_SCAN,R_BT_STATUS,R_BT_PAIR,R_BT_CONNECT,R_BT_DISCONNECT};
struct radio_item {char name[65],address[18];int security,signal;};
static struct radio_item radio_items[RADIO_CAP];
static int radio_count,radio_top,radio_index,radio_allow,radio_mode;
static enum radio_job radio_active;
static pid_t radio_pid=-1;
static int radio_fd=-1,radio_overflow;
static size_t radio_used;
static char radio_buffer[RADIO_OUT+1],radio_note[70],radio_ssid[33],radio_key[65];
static int radio_keyboard,radio_key_layer,radio_security;
static struct timespec radio_deadline;
static void radio_wipe(void *buf,size_t n){volatile unsigned char *p=buf;while(n--)*p++=0;}
static void radio_message(const char *msg){snprintf(radio_note,sizeof(radio_note),"%.69s",msg);}
static int radio_mac(const char *p){
    if(strlen(p)!=17)return 0;
    for(int i=0;i<17;i++)if(i%3==2?p[i]!=':':!isxdigit((unsigned char)p[i]))return 0;
    return 1;
}
static int radio_ssid_decode(const char *src,char dst[65]){
    size_t n=0;
    while(*src){
        unsigned c=(unsigned char)*src++;
        if(c=='\\'){
            if(!*src)return 0;
            c=(unsigned char)*src++;
            if(c=='x'){
                if(!src[0]||!src[1]||!isxdigit((unsigned char)src[0])||!isxdigit((unsigned char)src[1]))return 0;
                char tmp[3]={src[0],src[1],0};c=(unsigned)strtoul(tmp,NULL,16);src+=2;
            }else if(c!='\\'&&c!='"')return 0;
        }
        if(c<32||c>126||n>=32)return 0;
        dst[n++]=(char)c;
    }
    dst[n]=0;return n>0;
}
static void radio_parse_wifi(char *data){
    radio_count=radio_top=radio_index=0;
    char *save=NULL;
    for(char *line=strtok_r(data,"\n",&save);line&&radio_count<RADIO_CAP;line=strtok_r(NULL,"\n",&save)){
        char *fields[5];fields[0]=line;int valid=1;
        for(int i=1;i<5;i++){
            char *tab=strchr(fields[i-1],'\t');
            if(!tab){valid=0;break;}*tab=0;fields[i]=tab+1;
        }
        if(!valid||!radio_mac(fields[0]))continue;
        struct radio_item item={0};
        if(!radio_ssid_decode(fields[4],item.name))continue;
        if(strstr(fields[3],"PSK")&&!strstr(fields[3],"EAP"))item.security=1;
        else if(strstr(fields[3],"WPA")||strstr(fields[3],"WEP")||strstr(fields[3],"SAE")||strstr(fields[3],"OWE")||strstr(fields[3],"RSN"))item.security=2;
        char *end;long level=strtol(fields[2],&end,10);item.signal=end!=fields[2]&&!*end&&level>=-127&&level<=0?(int)level:-127;
        strcpy(item.address,fields[0]);
        int duplicate=-1;
        for(int i=0;i<radio_count;i++)if(!strcmp(radio_items[i].name,item.name)&&radio_items[i].security==item.security){duplicate=i;break;}
        if(duplicate>=0){if(item.signal>radio_items[duplicate].signal)radio_items[duplicate]=item;}
        else radio_items[radio_count++]=item;
    }
}
static void radio_parse_bt(char *data){
    radio_count=radio_top=radio_index=0;
    char *save=NULL;
    for(char *line=strtok_r(data,"\n",&save);line&&radio_count<RADIO_CAP;line=strtok_r(NULL,"\n",&save)){
        if(strncmp(line,"Device ",7)||strlen(line)<24)continue;
        struct radio_item item={0};memcpy(item.address,line+7,17);
        if(!radio_mac(item.address))continue;
        if(line[24]==' ')snprintf(item.name,sizeof(item.name),"%.64s",line+25);
        else strcpy(item.name,item.address);
        for(char *p=item.name;*p;p++)if((unsigned char)*p<32||(unsigned char)*p>126)*p='?';
        int duplicate=0;
        for(int i=0;i<radio_count;i++)if(!strcmp(radio_items[i].address,item.address))duplicate=1;
        if(!duplicate)radio_items[radio_count++]=item;
    }
}
static void radio_select(int step){
    if(!radio_count)return;
    radio_index=(radio_index+radio_count+step)%radio_count;
    if(radio_index<radio_top)radio_top=radio_index;
    if(radio_index>=radio_top+4)radio_top=radio_index-3;
}
static int radio_mutating(enum radio_job job){return job!=R_WIFI_STATUS&&job!=R_BT_STATUS;}
static int radio_spawn(enum radio_job job,const char *arg){
    if(radio_pid>=0){radio_message("Request already running");return 0;}
    if(radio_mutating(job)&&!radio_allow){radio_message("Radio changes locked");return 0;}
    const char *program=job>=R_BT_SCAN?"/usr/bin/pocketos-bluetooth":"/usr/bin/pocketos-wifi";
    const char *action=job==R_WIFI_SCAN?"scan-list":job==R_WIFI_STATUS?"status":job==R_WIFI_JOIN?"connect":job==R_WIFI_DISCONNECT?"disconnect":job==R_BT_SCAN?"discover":job==R_BT_STATUS?"status":job==R_BT_PAIR?"pair":job==R_BT_CONNECT?"connect":"disconnect";
    if(access(program,X_OK)!=0){radio_message("Radio helper not installed");return 0;}
    if(job>=R_BT_PAIR&&(!arg||!radio_mac(arg)))return 0;
    int out[2],in[2];
    if(pipe(out)!=0)return 0;
    if(pipe(in)!=0){close(out[0]);close(out[1]);return 0;}
    int fl=fcntl(out[0],F_GETFL);
    if(fl<0||fcntl(out[0],F_SETFL,fl|O_NONBLOCK)!=0||fcntl(out[0],F_SETFD,FD_CLOEXEC)!=0){
        close(out[0]);close(out[1]);close(in[0]);close(in[1]);return 0;
    }
    pid_t pid=fork();
    if(pid==0){
        (void)setpgid(0,0);
        close(out[0]);close(in[1]);
        if(dup2(out[1],STDOUT_FILENO)<0||dup2(in[0],STDIN_FILENO)<0)_exit(126);
        close(out[1]);close(in[0]);
        int nullfd=open("/dev/null",O_WRONLY|O_CLOEXEC);
        if(nullfd<0||dup2(nullfd,STDERR_FILENO)<0)_exit(126);
        close(nullfd);
        if(radio_mutating(job)){
            if(arg)execl(program,program,"--allow-radio",action,arg,(char*)NULL);
            else execl(program,program,"--allow-radio",action,(char*)NULL);
        }else execl(program,program,action,(char*)NULL);
        _exit(127);
    }
    close(out[1]);close(in[0]);
    if(pid<0){close(out[0]);close(in[1]);return 0;}
    (void)setpgid(pid,pid);
    if(job==R_WIFI_JOIN){
        char input[100];int len=snprintf(input,sizeof(input),"%s\n%s\n",radio_ssid,radio_key);
        if(len>0&&(size_t)len<sizeof(input)){ssize_t n=write(in[1],input,(size_t)len);(void)n;}
        radio_wipe(input,sizeof(input));radio_wipe(radio_key,sizeof(radio_key));
    }
    close(in[1]);
    radio_pid=pid;radio_fd=out[0];radio_active=job;radio_used=0;radio_overflow=0;
    if(clock_gettime(CLOCK_MONOTONIC,&radio_deadline)!=0)radio_deadline.tv_sec=0;
    else radio_deadline.tv_sec+=45;
    radio_message(job==R_WIFI_SCAN||job==R_BT_SCAN?"Scanning...":"Working...");
    return 1;
}
static void radio_drain(void){
    char chunk[512];ssize_t n;
    for(int i=0;i<64&&(n=read(radio_fd,chunk,sizeof(chunk)))>0;i++){
        size_t room=RADIO_OUT-radio_used;
        if((size_t)n>room){radio_overflow=1;continue;}
        memcpy(radio_buffer+radio_used,chunk,(size_t)n);radio_used+=(size_t)n;
    }
}
static int radio_poll(void){
    if(radio_pid<0)return 0;
    radio_drain();
    int status=0;pid_t pid=waitpid(radio_pid,&status,WNOHANG);
    struct timespec now;
    if(pid==0&&clock_gettime(CLOCK_MONOTONIC,&now)==0&&now.tv_sec<radio_deadline.tv_sec)return 0;
    int timed_out=pid==0;
    if(timed_out){(void)kill(-radio_pid,SIGKILL);(void)kill(radio_pid,SIGKILL);(void)waitpid(radio_pid,&status,0);}
    else if(pid<0&&errno==EINTR)return 0;
    radio_drain();
    close(radio_fd);radio_fd=-1;radio_pid=-1;radio_buffer[radio_used]=0;
    enum radio_job job=radio_active;radio_active=R_NONE;
    if(timed_out||pid<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0||radio_overflow){
        if(timed_out)radio_message("Radio request timed out");
        else if(strstr(radio_buffer,"RADIO_ERROR=wifi-start-failed"))radio_message("Wi-Fi driver or service failed to start");
        else if(strstr(radio_buffer,"RADIO_ERROR=supplicant-unavailable"))radio_message("Wi-Fi service not running");
        else if(strstr(radio_buffer,"RADIO_ERROR=scan-incomplete"))radio_message("Wi-Fi scan incomplete; retry");
        else if(strstr(radio_buffer,"RADIO_ERROR=bt-service"))radio_message("Bluetooth service unavailable");
        else if(strstr(radio_buffer,"RADIO_ERROR=bt-controller"))radio_message("Bluetooth controller not ready");
        else if(strstr(radio_buffer,"RADIO_ERROR=bt-power"))radio_message("Bluetooth adapter not powered");
        else radio_message("Request failed / service unavailable");
        return 1;
    }
    if(job==R_WIFI_SCAN){radio_parse_wifi(radio_buffer);radio_message(radio_count?"Choose a network":"No supported networks found");}
    else if(job==R_BT_SCAN){radio_parse_bt(radio_buffer);radio_message(radio_count?"Choose a device":"No devices listed");}
    else if(job==R_WIFI_STATUS){
        if(!strstr(radio_buffer,"wpa_state=COMPLETED"))radio_message("Wi-Fi not associated");
        else if(strstr(radio_buffer,"pocketos_ipv4=none"))radio_message("Wi-Fi associated; no IPv4 address");
        else if(strstr(radio_buffer,"pocketos_ipv4=unknown")||!strstr(radio_buffer,"pocketos_ipv4="))radio_message("Wi-Fi associated; IP not verified");
        else radio_message("Wi-Fi associated; IPv4 present");
    }else if(job==R_BT_STATUS){
        radio_message(strstr(radio_buffer,"Powered: yes")?"Bluetooth powered on":"Bluetooth not powered / unknown");
    }else if(job==R_WIFI_JOIN)radio_message("Association requested; check status");
    else if(job==R_WIFI_DISCONNECT)radio_message("Disconnect requested");
    else if(strstr(radio_buffer,"Failed")||strstr(radio_buffer,"not available"))radio_message("Bluetooth request failed");
    else radio_message("Bluetooth request sent; verify device");
    return 1;
}
static void radio_clear_key(void){radio_wipe(radio_key,sizeof(radio_key));radio_keyboard=radio_key_layer=0;}
static void radio_close(void){
    radio_clear_key();
    if(radio_pid>=0){(void)kill(-radio_pid,SIGKILL);(void)kill(radio_pid,SIGKILL);(void)waitpid(radio_pid,NULL,0);}
    if(radio_fd>=0)close(radio_fd);
    radio_pid=-1;radio_fd=-1;radio_active=R_NONE;
}
#endif
