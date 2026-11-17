/* PocketOS on-demand SD-only HTTP file transfer.
 * A single bounded process, no TLS, no Internet exposure by design: bind the
 * current wlan0 IPv4 address and require an eight-digit on-screen pairing key.
 * Use ONLY on a trusted local Wi-Fi network. NEVER port-forward this service.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TRANSFER_PORT 8765
#define IO_BUFFER 8192
#define MAX_UPLOAD (512ULL * 1024ULL * 1024ULL)
#define RESERVE_BYTES (16ULL * 1024ULL * 1024ULL)
static volatile sig_atomic_t running=1;
static char key[9],address[INET_ADDRSTRLEN];
static const char *root="/mnt/sd_0";
static int unauthorized=0;
#ifdef POCKETOS_TEST_FIXTURE
static int test_fixture=0;
#endif
static void stopping(int sig){(void)sig;running=0;}
static int random_bytes(unsigned char *p,size_t n){
    int f=open("/dev/urandom",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(f<0)return 0;
    size_t used=0;
    while(used<n){ssize_t z=read(f,p+used,n-used);if(z<=0){close(f);return 0;}used+=(size_t)z;}
    close(f);return 1;
}
static int mount_confirmed(void){
#ifdef POCKETOS_TEST_FIXTURE
    if(test_fixture)return 1;
#endif
    struct stat st,base;
    if(lstat(root,&st)!=0||!S_ISDIR(st.st_mode)||
       stat("/mnt",&base)!=0||st.st_dev==base.st_dev)return 0;
    FILE *f=fopen("/proc/mounts","r");
    if(!f)return 0;
    char device[256],point[256],fstype[64],opts[256];
    int ok=0;
    while(fscanf(f,"%255s %255s %63s %255s %*d %*d",device,point,fstype,opts)==4){
        if(!strcmp(point,"/mnt/sd_0")&&strstr(opts,"ro")==NULL){ok=1;break;}
    }
    fclose(f);
    return ok;
}
static int category(const char *name){
    return !strcmp(name,"Music")||!strcmp(name,"Books")||
           !strcmp(name,"Documents")||!strcmp(name,"Downloads");
}
static int filename_ok(const char *name){
    size_t n=strlen(name);
    if(n<1||n>120||!strcmp(name,".")||!strcmp(name,"..")||name[0]=='.')return 0;
    for(size_t i=0;i<n;i++){
        unsigned char ch=(unsigned char)name[i];
        if(ch<32||ch>126||ch=='/'||ch=='\\'||ch==':'||ch=='%'||ch=='?'||ch=='#')return 0;
    }
    return 1;
}
static int decode(char *dst,size_t cap,const char *src){
    size_t n=0;
    for(;*src;src++){
        unsigned char ch=(unsigned char)*src;
        if(ch=='%'){
            if(!isxdigit((unsigned char)src[1])||!isxdigit((unsigned char)src[2]))return 0;
            char pair[3]={src[1],src[2],0};
            ch=(unsigned char)strtoul(pair,NULL,16);src+=2;
        }
        if(n+1>=cap)return 0;
        dst[n++]=(char)ch;
    }
    dst[n]=0;return filename_ok(dst);
}
static int folder_open(const char *folder){
    if(!category(folder)||!mount_confirmed())return -1;
    int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(dir<0)return -1;
    int fd=openat(dir,folder,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    close(dir);return fd;
}
static int send_all(int fd,const void *buffer,size_t length){
    const unsigned char *p=buffer;
    while(length&&running){
        ssize_t n=send(fd,p,length,MSG_NOSIGNAL);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return 0;
        p+=(size_t)n;length-=(size_t)n;
    }
    return length==0;
}
static void head(int fd,int code,const char *reason,const char *type,unsigned long long length,int known){
    char b[512];
    int n=snprintf(b,sizeof(b),"HTTP/1.0 %d %s\r\nContent-Type: %s\r\n"
      "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
      "Referrer-Policy: no-referrer\r\nX-Frame-Options: DENY\r\n"
      "Connection: close\r\n"
      "%s",code,reason,type,known?"":"");
    if(n>0&&(size_t)n<sizeof(b))(void)send_all(fd,b,(size_t)n);
    if(known){
        n=snprintf(b,sizeof(b),"Content-Length: %llu\r\n\r\n",length);
        if(n>0&&(size_t)n<sizeof(b))(void)send_all(fd,b,(size_t)n);
    } else (void)send_all(fd,"\r\n",2);
}
static void message(int fd,int code,const char *reason,const char *detail){
    size_t n=strlen(detail);
    head(fd,code,reason,"text/plain; charset=utf-8",n,1);
    (void)send_all(fd,detail,n);
}
static int header_read(int fd,char buf[4096]){
    size_t len=0;
    while(len<4095&&running){
        ssize_t z=recv(fd,buf+len,1,0);
        if(z<0&&errno==EINTR)continue;
        if(z!=1)return 0;
        len++;
        if(len>=4&&!memcmp(buf+len-4,"\r\n\r\n",4)){buf[len]=0;return 1;}
    }
    return 0;
}
static int authorized(const char *header){
    const char *p=strstr(header,"\r\n");
    if(!p)return 0;
    p+=2;
    while(*p&&strncmp(p,"\r\n",2)){
        const char *e=strstr(p,"\r\n");if(!e)return 0;
        if((size_t)(e-p)>=17&&!strncasecmp(p,"X-PocketOS-Key: ",16)){
            if((size_t)(e-p)==24&&memcmp(p+16,key,8)==0)return 1;
        }
        p=e+2;
    }
    return 0;
}
static int content_length(const char *header,unsigned long long *length){
    const char *p=strstr(header,"\r\n");if(!p)return 0;p+=2;
    while(*p&&strncmp(p,"\r\n",2)){
        const char *e=strstr(p,"\r\n");if(!e)return 0;
        if((size_t)(e-p)>=16&&!strncasecmp(p,"Content-Length:",15)){
            const char *v=p+15;while(v<e&&*v==' ')v++;
            if(v==e)return 0;
            unsigned long long x=0;
            while(v<e){
                if(*v<'0'||*v>'9'||x>(MAX_UPLOAD/10ULL+10ULL))return 0;
                x=x*10+(unsigned)(*v++-'0');
            }
            if(x>MAX_UPLOAD)return 0;
            *length=x;return 1;
        }
        p=e+2;
    }
    return 0;
}
static int route_parts(const char *uri,const char *prefix,char folder[32],char name[128]){
    size_t n=strlen(prefix);
    if(strncmp(uri,prefix,n))return 0;
    const char *p=uri+n,*slash=strchr(p,'/');
    if(!slash||(size_t)(slash-p)>30||slash==p)return 0;
    memcpy(folder,p,(size_t)(slash-p));folder[slash-p]=0;
    if(!category(folder))return 0;
    return decode(name,128,slash+1);
}
static void json_string(int fd,const char *name){
    (void)send_all(fd,"\"",1);
    for(const unsigned char *p=(const unsigned char*)name;*p;p++){
        char escaped[8];
        if(*p=='"'||*p=='\\'){
            escaped[0]='\\';escaped[1]=(char)*p;
            (void)send_all(fd,escaped,2);
        }else if(*p>=32&&*p<127)(void)send_all(fd,p,1);
    }
    (void)send_all(fd,"\"",1);
}
static void list_files(int fd,const char *folder){
    int d=folder_open(folder);
    if(d<0){message(fd,409,"Conflict","microSD folder unavailable");return;}
    DIR *dir=fdopendir(d);
    if(!dir){close(d);message(fd,500,"Error","Failed to open directory");return;}
    head(fd,200,"OK","application/json; charset=utf-8",0,0);
    (void)send_all(fd,"[",1);
    struct dirent *entry;int count=0;
    while((entry=readdir(dir))&&count<128&&running){
        if(!filename_ok(entry->d_name))continue;
        struct stat st;
        if(fstatat(d,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0||!S_ISREG(st.st_mode))continue;
        if(count)(void)send_all(fd,",",1);
        json_string(fd,entry->d_name);
        count++;
    }
    (void)send_all(fd,"]",1);
    closedir(dir);
}
static void download_file(int fd,const char *folder,const char *name){
    int d=folder_open(folder);
    if(d<0){message(fd,409,"Conflict","microSD folder unavailable");return;}
    int file=openat(d,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(file<0||fstat(file,&st)!=0||!S_ISREG(st.st_mode)){
        if(file>=0)close(file);close(d);
        message(fd,404,"Not Found","File unavailable");return;
    }
    head(fd,200,"OK","application/octet-stream",(unsigned long long)st.st_size,1);
    char buf[IO_BUFFER];ssize_t n;
    while(running&&(n=read(file,buf,sizeof(buf)))>0)
        if(!send_all(fd,buf,(size_t)n))break;
    close(file);close(d);
}
static void upload_file(int fd,const char *header,const char *folder,const char *name){
    unsigned long long bytes;
    if(!content_length(header,&bytes)){message(fd,411,"Length Required","Valid Content-Length required (max 512 MiB)");return;}
    int d=folder_open(folder);
    if(d<0){message(fd,409,"Conflict","microSD folder unavailable");return;}
    struct statvfs st;
    if(fstatvfs(d,&st)!=0||st.f_frsize==0||
       (unsigned long long)st.f_bavail*(unsigned long long)st.f_frsize<bytes+RESERVE_BYTES){
        close(d);message(fd,507,"Insufficient Storage","Not enough SD space");return;
    }
    int file=openat(d,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(file<0){close(d);message(fd,409,"Conflict","File already exists or invalid filename");return;}
    int ok=1;
    char buf[IO_BUFFER];
    while(bytes&&running){
        size_t want=bytes<sizeof(buf)?(size_t)bytes:sizeof(buf);
        ssize_t got=recv(fd,buf,want,0);
        if(got<0&&errno==EINTR)continue;
        if(got<=0){ok=0;break;}
        size_t off=0;
        while(off<(size_t)got){
            ssize_t w=write(file,buf+off,(size_t)got-off);
            if(w<0&&errno==EINTR)continue;
            if(w<=0){ok=0;break;}
            off+=(size_t)w;
        }
        if(!ok)break;
        bytes-=(unsigned long long)got;
    }
    if(bytes||!running)ok=0;
    if(ok&&fsync(file)!=0)ok=0;
    if(close(file)!=0)ok=0;
    if(!ok)(void)unlinkat(d,name,0);
    close(d);
    if(ok)message(fd,201,"Created","File saved on microSD");
    else message(fd,500,"Error","Upload interrupted or storage failure; partial file removed");
}
static const char html[]=
"<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>PocketOS Transfer</title>"
"<style>body{font:16px system-ui;background:#101820;color:#ecf8f5;max-width:650px;margin:32px auto;padding:14px}"
"section{background:#1b2c36;border-radius:16px;padding:20px;margin:15px 0}button,input,select{font:inherit;"
"padding:11px;border-radius:10px;margin:5px}button{background:#73edc4;color:#101820;border:0}"
"a{color:#73edc4}.hint{color:#b5c9cc}li{padding:8px}</style>"
"<h1>PocketOS Wireless File Transfer</h1><p class=hint>Trusted local Wi-Fi only. Files are saved on microSD.</p>"
"<section><label>Eight-digit code from your PocketOS screen<br><input id=pin maxlength=8 inputmode=numeric "
"autocomplete=off placeholder='Pairing code'></label><button id=pair>Connect</button><p id=msg></p></section>"
"<section><label>Folder <select id=folder><option>Music</option><option>Books</option>"
"<option>Documents</option><option>Downloads</option></select></label>"
"<input id=file type=file><button id=upload>Upload</button><p id=progress></p>"
"<button id=refresh>Show files</button><ul id=files></ul></section>"
"<script>let token='';const $=id=>document.getElementById(id);"
"const enc=x=>encodeURIComponent(x);"
"async function api(path,opts={}){opts.headers={...(opts.headers||{}),'X-PocketOS-Key':token};"
"const r=await fetch(path,opts);if(!r.ok)throw Error(await r.text());return r;}"
"$('pair').onclick=async()=>{token=$('pin').value.trim();$('pin').value='';"
"if(!/^\\d{8}$/.test(token)){token='';$('msg').textContent='Enter the 8-digit code';return;}"
"try{await listing();$('msg').textContent='Connected';}catch(e){token='';$('msg').textContent=e.message;}};"
"async function listing(){if(!token)throw Error('Enter pairing code first');"
"const folder=$('folder').value,r=await api('/list/'+enc(folder)),a=await r.json();"
"$('files').replaceChildren();for(const name of a){const li=document.createElement('li');"
"const button=document.createElement('button');button.textContent='Download '+name;"
"button.onclick=async()=>{try{const r=await api('/file/'+enc(folder)+'/'+enc(name));"
"const blob=await r.blob(),url=URL.createObjectURL(blob),a=document.createElement('a');"
"a.href=url;a.download=name;a.click();URL.revokeObjectURL(url);}catch(e){$('msg').textContent=e.message;}};"
"li.append(button);$('files').append(li);}}"
"$('refresh').onclick=()=>listing().catch(e=>$('msg').textContent=e.message);"
"$('upload').onclick=async()=>{const f=$('file').files[0];if(!f||!token){$('progress').textContent='Select a file and connect first';return;}"
"if(f.size>536870912){$('progress').textContent='Maximum 512 MiB per file';return;}"
"$('progress').textContent='Uploading '+f.name+'...';"
"try{await api('/file/'+enc($('folder').value)+'/'+enc(f.name),{method:'PUT',body:f});"
"$('progress').textContent='Saved to microSD';await listing();}catch(e){$('progress').textContent=e.message;}};"
"</script>";
static void serve(int fd){
    char headers[4096],method[8],uri[420],protocol[16];
    if(!header_read(fd,headers)){message(fd,400,"Bad Request","Invalid headers");return;}
    if(sscanf(headers,"%7s %419s %15s",method,uri,protocol)!=3||
       strncmp(protocol,"HTTP/1.",7)){message(fd,400,"Bad Request","Invalid request");return;}
    if(!strcmp(method,"GET")&&!strcmp(uri,"/")){
        head(fd,200,"OK","text/html; charset=utf-8",sizeof(html)-1,1);
        (void)send_all(fd,html,sizeof(html)-1);return;
    }
    if(!authorized(headers)){
        unauthorized++;
        message(fd,403,"Forbidden","Invalid pairing code");
        if(unauthorized>=5)running=0;
        return;
    }
    char folder[32],name[128];
    if(!strcmp(method,"GET")&&!strncmp(uri,"/list/",6)&&category(uri+6)){
        list_files(fd,uri+6);
    }else if(route_parts(uri,"/file/",folder,name)){
        if(!strcmp(method,"GET"))download_file(fd,folder,name);
        else if(!strcmp(method,"PUT"))upload_file(fd,headers,folder,name);
        else message(fd,405,"Method Not Allowed","GET or PUT only");
    }else message(fd,404,"Not Found","Unknown transfer path");
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS Transfer v1");return 0;}
#ifdef POCKETOS_TEST_FIXTURE
    if(argc==3&&!strcmp(argv[1],"--fixture-root")&&argv[2][0]=='/'){
        test_fixture=1;root=argv[2];
        if(strlen(root)>400||strstr(root,".."))return 2;
    }else
#endif
    if(argc!=1)return 2;
    if(!mount_confirmed()){fputs("Mounted microSD is required\n",stderr);return 1;}
    int rootfd=open(root,O_DIRECTORY|O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(rootfd<0)return 1;
    close(rootfd);
    unsigned char secret[8];
    if(!random_bytes(secret,sizeof(secret)))return 1;
    unsigned long long v=0;for(size_t i=0;i<sizeof(secret);i++)v=(v<<8)|secret[i];
    snprintf(key,sizeof(key),"%08llu",v%100000000ULL);
    int srv=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(srv<0)return 1;
    struct sockaddr_in bindaddr={.sin_family=AF_INET,.sin_port=htons(TRANSFER_PORT)};
#ifdef POCKETOS_TEST_FIXTURE
    if(test_fixture){bindaddr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);}
    else
#endif
    {
        struct ifreq ifr={0};
        snprintf(ifr.ifr_name,sizeof(ifr.ifr_name),"wlan0");
        if(ioctl(srv,SIOCGIFADDR,&ifr)!=0){perror("Wi-Fi IPv4 unavailable");close(srv);return 1;}
        bindaddr.sin_addr=((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr;
    }
    if(!inet_ntop(AF_INET,&bindaddr.sin_addr,address,sizeof(address))){close(srv);return 1;}
    int opt=1;(void)setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(bind(srv,(struct sockaddr*)&bindaddr,sizeof(bindaddr))!=0||listen(srv,2)!=0){
        perror("Cannot bind local transfer socket");close(srv);return 1;
    }
    struct sigaction sa={.sa_handler=stopping};sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGTERM,&sa,NULL);(void)sigaction(SIGINT,&sa,NULL);
    /* This goes to the UI pipe; the secret is never logged to disk. */
    printf("http://%s:%u\n%s\n",address,TRANSFER_PORT,key);
    fflush(stdout);
    time_t last=time(NULL);
    while(running){
        struct pollfd p={.fd=srv,.events=POLLIN};
        int rc=poll(&p,1,1000);
        if(rc<0){if(errno==EINTR)continue;break;}
        if(rc==0){if(time(NULL)-last>900)break;continue;}
        if(!(p.revents&POLLIN))break;
        int client=accept(srv,NULL,NULL);
        if(client<0){if(errno==EINTR)continue;break;}
        struct timeval tv={.tv_sec=20};
        (void)setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        (void)setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
        serve(client);
        close(client);
        last=time(NULL);
    }
    close(srv);
    memset(key,0,sizeof(key));
    return 0;
}
