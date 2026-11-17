/* PocketOS Wi-Fi control: bounded direct wpa_supplicant socket, no shell. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int fd=-1, monitor=-1;
static char tempdir[]="/tmp/pocketos-wpa-XXXXXX", local[108], event_local[108];
static void cleanup(void){if(fd>=0)close(fd);if(monitor>=0){(void)send(monitor,"DETACH",6,MSG_DONTWAIT);close(monitor);}if(local[0])unlink(local);if(event_local[0])unlink(event_local);rmdir(tempdir);}
static int connect_socket(const char *path,const char *name,char address[108]){
    struct sockaddr_un a={.sun_family=AF_UNIX},b={.sun_family=AF_UNIX};
    int n=snprintf(address,108,"%s/%s",tempdir,name);
    if(n<0||(size_t)n>=sizeof(a.sun_path)||strlen(path)>=sizeof(b.sun_path))return -1;
    strcpy(a.sun_path,address);strcpy(b.sun_path,path);
    int sock=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if(sock<0)return -1;
    if(bind(sock,(struct sockaddr*)&a,sizeof(a))!=0||connect(sock,(struct sockaddr*)&b,sizeof(b))!=0){close(sock);return -1;}
    return sock;
}
static int start(const char *path){
    if(!mkdtemp(tempdir))return 0;
    if(atexit(cleanup)!=0){rmdir(tempdir);return 0;}
    fd=connect_socket(path,"ctrl",local);return fd>=0;
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
/* Listen on a separate attached socket before starting the scan. Never
 * report stale cached results as a successful new scan. */
static int scan_complete(const char *path){
    monitor=connect_socket(path,"events",event_local);
    if(monitor<0||send(monitor,"ATTACH",6,0)!=6)return 0;
    struct pollfd p={.fd=monitor,.events=POLLIN};char r[1024];
    if(poll(&p,1,3000)!=1||!(p.revents&POLLIN))return 0;
    ssize_t n=recv(monitor,r,sizeof(r)-1,0);
    if(n<=0)return 0;
    r[n]=0;if(strcmp(r,"OK\n")&&strcmp(r,"OK"))return 0;
    if(!ok("SCAN"))return 0;
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    long long until=(long long)t.tv_sec*1000+t.tv_nsec/1000000+12000;
    for(;;){
        if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
        long long remaining=until-((long long)t.tv_sec*1000+t.tv_nsec/1000000);
        if(remaining<=0)return 0;
        int rc=poll(&p,1,(int)remaining);
        if(rc<0&&errno==EINTR)continue;
        if(rc!=1||!(p.revents&POLLIN))return 0;
        n=recv(monitor,r,sizeof(r)-1,0);if(n<=0)return 0;r[n]=0;
        const char *kind=strchr(r,'>');kind=kind?kind+1:r;
        if(!strncmp(kind,"CTRL-EVENT-SCAN-RESULTS",sizeof("CTRL-EVENT-SCAN-RESULTS")-1))return 1;
        if(!strncmp(kind,"CTRL-EVENT-SCAN-FAILED",sizeof("CTRL-EVENT-SCAN-FAILED")-1)||!strncmp(kind,"CTRL-EVENT-TERMINATING",sizeof("CTRL-EVENT-TERMINATING")-1))return 0;
    }
}
static void ipv4_status(void){
    struct ifaddrs *all=NULL;int found=0;
    if(getifaddrs(&all)!=0){puts("pocketos_ipv4=unknown");return;}
    for(struct ifaddrs *a=all;a;a=a->ifa_next){
        if(!a->ifa_addr||strcmp(a->ifa_name,"wlan0")||a->ifa_addr->sa_family!=AF_INET)continue;
        char buf[INET_ADDRSTRLEN];struct sockaddr_in *in=(struct sockaddr_in*)a->ifa_addr;
        if(in->sin_addr.s_addr&&inet_ntop(AF_INET,&in->sin_addr,buf,sizeof(buf))){printf("pocketos_ipv4=%s\n",buf);found=1;break;}
    }
    freeifaddrs(all);if(!found)puts("pocketos_ipv4=none");
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
/* Preserve enabled profiles across SELECT_NETWORK, which disables the others.
 * Reject an incomplete list before changing anything. Never read a saved PSK. */
#define SAVED_CAP 64
static int saved_enabled(int ids[SAVED_CAP],int *count){
    char r[16384];*count=0;
    if(!request("LIST_NETWORKS",r,sizeof(r)))return 0;
    char *line=strchr(r,'\n');
    if(strncmp(r,"network id",10)||!line)return 0;
    ++line;
    while(*line){
        char *next=strchr(line,'\n');if(!next)return 0;*next=0;
        char *end;errno=0;long id=strtol(line,&end,10);
        if(errno||end==line||*end!='\t'||id<0||id>999999)return 0;
        char *flags=strrchr(end+1,'\t');
        if(!flags||flags==end+1)return 0;
        if(!strstr(flags,"[DISABLED]")&&!strstr(flags,"[P2P-PERSISTENT]")){
            if(*count>=SAVED_CAP)return 0;
            ids[(*count)++]=(int)id;
        }
        line=next+1;
    }
    return 1;
}
static int restore_enabled(const int *ids,int count){
    int success=1;char cmd[48];
    for(int i=0;i<count;i++){
        snprintf(cmd,sizeof(cmd),"ENABLE_NETWORK %d",ids[i]);
        if(!ok(cmd))success=0;
    }
    return success;
}
static int associated_id(int id){
    char r[4096],wanted[32];int state=0,match=0;
    snprintf(wanted,sizeof(wanted),"id=%d",id);
    if(!request("STATUS",r,sizeof(r)))return 0;
    char *save=NULL;
    for(char *s=strtok_r(r,"\n",&save);s;s=strtok_r(NULL,"\n",&save)){
        if(!strcmp(s,"wpa_state=COMPLETED"))state=1;
        if(!strcmp(s,wanted))match=1;
    }
    return state&&match;
}
static int wait_for_association(int id){
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now)!=0)return 0;
    time_t deadline=now.tv_sec+15;
    do{
        if(associated_id(id))return 1;
        struct timespec pause={.tv_nsec=500000000L};
        (void)nanosleep(&pause,NULL);
        if(clock_gettime(CLOCK_MONOTONIC,&now)!=0)return 0;
    }while(now.tv_sec<deadline);
    return 0;
}
static int join_network(void){
    char ssid[34]={0},key[66]={0},hex[65]={0},secret[132]={0},cmd[256]={0},reply[256];
    int result=1,id=-1,selected=0,count=0,enabled[SAVED_CAP];
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
    if(!saved_enabled(enabled,&count)){puts("RADIO_ERROR=profile-list");goto done;}
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
    selected=1;
    if(!ok(cmd))goto done;
    /* Association must be for the new profile, not a stale IP or old SSID. */
    if(!wait_for_association(id)){puts("RADIO_ERROR=association-failed");goto done;}
    if(!restore_enabled(enabled,count)){puts("RADIO_ERROR=profile-restore");goto done;}
    /* Save only after the requested network authenticated successfully.
     * RAM-only configurations deliberately reject SAVE_CONFIG. */
    if(!ok("SAVE_CONFIG"))puts("RADIO_NOTICE=not-saved");
    puts("RADIO_ASSOCIATED=1");
    result=0;
done:
    if(result&&id>=0){
        snprintf(cmd,sizeof(cmd),"REMOVE_NETWORK %d",id);
        if(!ok(cmd))puts("RADIO_ERROR=profile-cleanup");
        if(selected){
            if(!restore_enabled(enabled,count))puts("RADIO_ERROR=profile-restore");
            if(count)(void)ok("RECONNECT");
        }
    }
    wipe(key,sizeof(key));wipe(secret,sizeof(secret));wipe(cmd,sizeof(cmd));
    if(result)fputs("Wi-Fi request failed or invalid input; inspect supplicant status.\n",stderr);
    return result;
}
static int ensure_service(void){
    pid_t pid=fork();
    if(pid<0)return 0;
    if(pid==0){execl("/usr/sbin/pocketos-radio-service","pocketos-radio-service","wifi-start",(char*)NULL);_exit(127);}
    int status;pid_t got;
    do{got=waitpid(pid,&status,0);}while(got<0&&errno==EINTR);
    return got==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS Wi-Fi v1");return 0;}
    const char *socket="/var/run/wpa_supplicant/wlan0";int opt=0,i=1,custom=0;
    if(i+1<argc&&!strcmp(argv[i],"--socket")){socket=argv[i+1];i+=2;custom=1;}
    if(i<argc&&!strcmp(argv[i],"--allow-radio")){opt=1;i++;}
    if(i+1!=argc){fputs("Usage: pocketos-wifi [--socket PATH] [--allow-radio] status|scan|results|connect|reconnect|disconnect\n",stderr);return 2;}
    const char *action=argv[i],*cmd=NULL;int mut=0;
    if(!strcmp(action,"status"))cmd="STATUS";
    else if(!strcmp(action,"results"))cmd="SCAN_RESULTS";
    else if(!strcmp(action,"scan")){cmd="SCAN";mut=1;}
    else if(!strcmp(action,"scan-list")){cmd="SCAN";mut=1;}
    else if(!strcmp(action,"disconnect")){cmd="DISCONNECT";mut=1;}
    else if(!strcmp(action,"reconnect")){cmd="RECONNECT";mut=1;}
    else if(!strcmp(action,"connect"))mut=1;
    else return 2;
    if(mut&&!opt){fputs("Radio actions require explicit --allow-radio.\n",stderr);return 2;}
    if(!custom&&access("/usr/sbin/pocketos-radio-service",X_OK)==0){
        socket="/run/pocketos-wifi/wlan0";
        if(mut&&strcmp(action,"disconnect")&&!ensure_service()){
            puts("RADIO_ERROR=wifi-start-failed");return 1;
        }
    }
    if(!start(socket)){puts("RADIO_ERROR=supplicant-unavailable");return 1;}
    if(!cmd)return join_network();
    if(!strcmp(action,"scan-list")){
        if(!scan_complete(socket)){puts("RADIO_ERROR=scan-incomplete");return 1;}
        cmd="SCAN_RESULTS";mut=0;
    }
    /* Explicit disconnect disables saved profiles without deleting credentials.
     * Reconnect enables them again. RAM-only configurations may reject saving. */
    if(!strcmp(action,"disconnect")){
        if(!ok("DISCONNECT")||!ok("DISABLE_NETWORK all"))return 1;
        if(!ok("SAVE_CONFIG"))puts("RADIO_NOTICE=not-saved");
        return 0;
    }
    if(!strcmp(action,"reconnect")){
        if(!ok("ENABLE_NETWORK all")||!ok("RECONNECT"))return 1;
        if(!ok("SAVE_CONFIG"))puts("RADIO_NOTICE=not-saved");
        return 0;
    }
    if(mut)return ok(cmd)?0:1;
    char r[16384];
    if(!request(cmd,r,sizeof(r))||!strncmp(r,"FAIL",4))return 1;
    fputs(r,stdout);if(!custom&&!strcmp(action,"status"))ipv4_status();return 0;
}
