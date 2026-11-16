/* PocketOS Wi-Fi control: bounded direct wpa_supplicant socket, no shell. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int fd=-1;
static char tempdir[]="/tmp/pocketos-wpa-XXXXXX", local[108];
static void cleanup(void){if(fd>=0)close(fd);if(local[0])unlink(local);rmdir(tempdir);}
static int start(const char *path){
    struct sockaddr_un a={.sun_family=AF_UNIX},b={.sun_family=AF_UNIX};
    if(strlen(path)>=sizeof(b.sun_path)||!mkdtemp(tempdir))return 0;
    if(atexit(cleanup)!=0){rmdir(tempdir);return 0;}
    int n=snprintf(local,sizeof(local),"%s/ctrl",tempdir);
    if(n<0||(size_t)n>=sizeof(local)||strlen(local)>=sizeof(a.sun_path))return 0;
    strcpy(a.sun_path,local);strcpy(b.sun_path,path);
    fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);
    return fd>=0&&bind(fd,(struct sockaddr*)&a,sizeof(a))==0&&connect(fd,(struct sockaddr*)&b,sizeof(b))==0;
}
static int request(const char *cmd,char *response,size_t size){
    size_t n=strlen(cmd);
    if(send(fd,cmd,n,0)!=(ssize_t)n)return 0;
    struct pollfd p={.fd=fd,.events=POLLIN};
    if(poll(&p,1,3000)!=1||!(p.revents&POLLIN))return 0;
    ssize_t got=recv(fd,response,size-1,MSG_TRUNC);
    if(got<=0||(size_t)got>=size)return 0;
    response[got]=0;
    return 1;
}
static int ok(const char *cmd){
    char r[256];
    return request(cmd,r,sizeof(r))&&(!strcmp(r,"OK\n")||!strcmp(r,"OK"));
}
static void wipe(char *p,size_t n){volatile char *q=p;while(n--)*q++=0;}
static int line(char *b,size_t n){
    if(!fgets(b,(int)n,stdin))return 0;
    size_t k=strlen(b);
    if(!k||b[k-1]!='\n')return 0;
    b[k-1]=0;
    return 1;
}
static int read_key(char *b,size_t n){
    if(!isatty(STDIN_FILENO))return line(b,n);
    struct termios old,next;
    if(tcgetattr(STDIN_FILENO,&old)!=0)return 0;
    next=old;next.c_lflag&=(tcflag_t)~ECHO;
    if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&next)!=0)return 0;
    fputs("Wi-Fi password (not echoed; empty for open network): ",stderr);
    int yes=line(b,n);
    if(tcsetattr(STDIN_FILENO,TCSANOW,&old)!=0)yes=0;
    fputc('\n',stderr);return yes;
}
static int join_network(void){
    char ssid[34]={0},key[66]={0},hex[65]={0},secret[132]={0},cmd[256]={0},reply[256];
    int result=1,id=-1;
    if(isatty(STDIN_FILENO))fputs("SSID: ",stderr);
    if(!line(ssid,sizeof(ssid))||!ssid[0]||strlen(ssid)>32||!read_key(key,sizeof(key)))goto done;
    size_t len=strlen(key);
    if(len&&len<8)goto done;
    if(len==64){for(size_t i=0;i<len;i++)if(!isxdigit((unsigned char)key[i]))goto done;strcpy(secret,key);}
    else if(len){
        size_t j=0;secret[j++]='"';
        for(size_t i=0;i<len;i++){unsigned char c=(unsigned char)key[i];if(c<32||c>126)goto done;if(c=='"'||c=='\\')secret[j++]='\\';secret[j++]=(char)c;}
        secret[j++]='"';secret[j]=0;
    }
    for(size_t i=0;i<strlen(ssid);i++)snprintf(hex+i*2,3,"%02x",(unsigned char)ssid[i]);
    if(!request("ADD_NETWORK",reply,sizeof(reply)))goto done;
    char *end;errno=0;long n=strtol(reply,&end,10);
    if(end==reply||errno||n<0||n>999999||(*end&&strcmp(end,"\n")))goto done;
    id=(int)n;
    snprintf(cmd,sizeof(cmd),"SET_NETWORK %d ssid %s",id,hex);
    if(!ok(cmd))goto done;
    snprintf(cmd,sizeof(cmd),"SET_NETWORK %d key_mgmt %s",id,len?"WPA-PSK":"NONE");
    if(!ok(cmd))goto done;
    if(len){
        snprintf(cmd,sizeof(cmd),"SET_NETWORK %d psk %s",id,secret);
        if(!ok(cmd))goto done;
    }
    snprintf(cmd,sizeof(cmd),"SELECT_NETWORK %d",id);
    if(!ok(cmd))goto done;
    puts("Association requested; check status. No credentials saved by PocketOS.");
    result=0;
done:
    if(result&&id>=0){snprintf(cmd,sizeof(cmd),"REMOVE_NETWORK %d",id);(void)ok(cmd);}
    wipe(key,sizeof(key));wipe(secret,sizeof(secret));wipe(cmd,sizeof(cmd));
    if(result)fputs("Wi-Fi request failed or invalid input; inspect supplicant status.\n",stderr);
    return result;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS Wi-Fi v1");return 0;}
    const char *socket="/var/run/wpa_supplicant/wlan0";int opt=0,i=1;
    if(i+1<argc&&!strcmp(argv[i],"--socket")){socket=argv[i+1];i+=2;}
    if(i<argc&&!strcmp(argv[i],"--allow-radio")){opt=1;i++;}
    if(i+1!=argc){fputs("Usage: pocketos-wifi [--socket PATH] [--allow-radio] status|scan|results|connect|disconnect\n",stderr);return 2;}
    const char *action=argv[i],*cmd=NULL;int mut=0;
    if(!strcmp(action,"status"))cmd="STATUS";
    else if(!strcmp(action,"results"))cmd="SCAN_RESULTS";
    else if(!strcmp(action,"scan")){cmd="SCAN";mut=1;}
    else if(!strcmp(action,"scan-list")){cmd="SCAN";mut=1;}
    else if(!strcmp(action,"disconnect")){cmd="DISCONNECT";mut=1;}
    else if(!strcmp(action,"connect"))mut=1;
    else return 2;
    if(mut&&!opt){fputs("Radio actions require explicit --allow-radio.\n",stderr);return 2;}
    if(!start(socket)){fputs("Supplicant control socket unavailable.\n",stderr);return 1;}
    if(!cmd)return join_network();
    if(!strcmp(action,"scan-list")){
        if(!ok("SCAN"))return 1;
        struct timespec delay={.tv_sec=4,.tv_nsec=0};
        while(nanosleep(&delay,&delay)<0&&errno==EINTR){}
        cmd="SCAN_RESULTS";mut=0;
    }
    if(mut)return ok(cmd)?0:1;
    char r[16384];
    if(!request(cmd,r,sizeof(r))||!strncmp(r,"FAIL",4))return 1;
    fputs(r,stdout);return 0;
}
