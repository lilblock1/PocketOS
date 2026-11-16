/* PocketOS bounded WAV streaming. Hardware output is explicitly opt-in. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct wav { FILE *f; uint32_t bytes,rate; unsigned channels,bits,frame; off_t offset; int decoder_fd; pid_t decoder; };
static uint32_t le(const unsigned char *p,unsigned n){
    uint32_t v=0;for(unsigned i=0;i<n;i++)v|=(uint32_t)p[i]<<(i*8);return v;
}
static int readbytes(FILE *f,void *p,size_t n){return fread(p,1,n,f)==n;}
static int parse(struct wav *w,const char *path){
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
    struct stat st;
    if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<12){close(fd);return 0;}
    w->f=fdopen(fd,"rb");if(!w->f){close(fd);return 0;}
    unsigned char h[16];
    if(!readbytes(w->f,h,12)||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4))return 0;
    uint64_t end=(uint64_t)le(h+4,4)+8;
    if(end>(uint64_t)st.st_size||end<12)return 0;
    int fmt=0,data=0;
    for(unsigned chunks=0;chunks<4096;chunks++){
        off_t pos=ftello(w->f);
        if(pos<0||(uint64_t)pos+8>end)break;
        if(!readbytes(w->f,h,8))return 0;
        unsigned n=le(h+4,4);
        uint64_t start=(uint64_t)pos+8,next=start+n+(n&1u);
        if(next>end)return 0;
        if(!memcmp(h,"fmt ",4)){
            if(fmt||n<16||!readbytes(w->f,h,16)||le(h,2)!=1)return 0;
            w->channels=le(h+2,2);w->rate=le(h+4,4);w->bits=le(h+14,2);
            if(w->channels<1||w->channels>2||w->rate<8000||w->rate>192000||(w->bits!=16&&w->bits!=24))return 0;
            w->frame=w->channels*(w->bits/8);
            if(le(h+12,2)!=w->frame||le(h+8,4)!=w->rate*w->frame)return 0;
            fmt=1;
        } else if(!memcmp(h,"data",4)){
            if(data)return 0;
            w->offset=(off_t)start;w->bytes=n;data=1;
        }
        if(fmt&&data)break;
        if(fseeko(w->f,(off_t)next,SEEK_SET)!=0)return 0;
    }
    return fmt&&data&&w->bytes>0&&w->bytes%w->frame==0&&fseeko(w->f,w->offset,SEEK_SET)==0;
}
static void gain(unsigned char *p,size_t n,unsigned bits,int volume){
    if(volume==100)return;
    if(volume==0){memset(p,0,n);return;}
    unsigned stride=bits/8,sign=1u<<(bits-1),mod=1u<<bits;
    for(size_t i=0;i<n;i+=stride){
        uint32_t raw=le(p+i,stride);
        int sample=raw&sign?(int)raw-(int)mod:(int)raw;
        /* squared percentage: no amplify; default 25% is about -24 dB */
        int32_t scaled=(int32_t)((int64_t)sample*volume*volume/10000);
        for(unsigned j=0;j<stride;j++)p[i+j]=(unsigned char)((uint32_t)scaled>>(8*j));
    }
}
static volatile sig_atomic_t interrupted=0;
static int stopped=0;
static void stop_signal(int n){(void)n;interrupted=1;}
/* Local file descriptors only; no URL input, shell, or network protocols. */
static const char *demuxer(const char *path){
    const char *e=strrchr(path,'.');if(!e)return NULL;
    if(!strcasecmp(e,".flac"))return "flac";
    if(!strcasecmp(e,".mp3"))return "mp3";
    if(!strcasecmp(e,".aac"))return "aac";
    if(!strcasecmp(e,".m4a")||!strcasecmp(e,".mp4"))return "mov";
    if(!strcasecmp(e,".ogg")||!strcasecmp(e,".oga")||!strcasecmp(e,".opus"))return "ogg";
    return NULL;
}
static int open_decoder(struct wav *w,const char *path,const char *format){
    if(access("/usr/bin/ffmpeg",X_OK)!=0){fputs("Optional FFmpeg decoder unavailable\n",stderr);return 0;}
    int input=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat st;
    if(input<0)return 0;
    if(fstat(input,&st)!=0||!S_ISREG(st.st_mode)){close(input);return 0;}
    int p[2];if(pipe(p)!=0){close(input);return 0;}
    pid_t child=fork();
    if(child==0){
        close(p[0]);
        if(dup2(p[1],STDOUT_FILENO)<0)_exit(126);
        close(p[1]);
        if(dup2(input,3)<0||fcntl(3,F_SETFD,0)<0)_exit(126);
        if(input!=3)close(input);
        execl("/usr/bin/ffmpeg","ffmpeg","-nostdin","-hide_banner","-loglevel","error",
              "-threads","1","-filter_threads","1","-max_alloc","16777216",
              "-probesize","262144","-analyzeduration","1000000",
              "-protocol_whitelist","file,pipe","-f",format,"-i","/proc/self/fd/3",
              "-map","0:a:0","-vn","-sn","-dn","-ac","2","-ar","48000",
              "-c:a","pcm_s16le","-f","s16le","pipe:1",(char*)NULL);
        _exit(127);
    }
    close(input);close(p[1]);
    if(child<0){close(p[0]);return 0;}
    int flags=fcntl(p[0],F_GETFL);
    if(flags<0||fcntl(p[0],F_SETFL,flags|O_NONBLOCK)!=0){
        close(p[0]);(void)kill(child,SIGTERM);(void)waitpid(child,NULL,0);return 0;
    }
    w->decoder_fd=p[0];w->decoder=child;w->rate=48000;w->channels=2;w->bits=16;w->frame=4;
    return 1;
}
static int finish_decoder(struct wav *w,int abort_decode){
    if(w->decoder<=0)return 0;
    close(w->decoder_fd);
    if(abort_decode)(void)kill(w->decoder,SIGKILL);
    int status=0;pid_t got;
    do{got=waitpid(w->decoder,&status,0);}while(got<0&&errno==EINTR);
    return !abort_decode&&(got!=w->decoder||!WIFEXITED(status)||WEXITSTATUS(status)!=0);
}
static int stream(struct wav *w,int out,int volume,int controlled){
    unsigned char b[8192],commands[64];
    size_t left=w->bytes,used=0,at=0;
    int paused=0,eof=0;
    while(!interrupted&&(left||at<used||(w->decoder>0&&!eof))){
        if(controlled){
            struct pollfd c={.fd=STDIN_FILENO,.events=POLLIN};
            int pc=poll(&c,1,paused?500:0);
            if(pc<0){if(errno==EINTR)continue;return 1;}
            if(pc>0){
                if(!(c.revents&POLLIN))return 2;
                ssize_t z=read(STDIN_FILENO,commands,sizeof(commands));
                if(z<=0)return 2;
                for(ssize_t j=0;j<z;j++){
                    if(commands[j]<=100)volume=commands[j];
                    else if(commands[j]==200)paused=1;
                    else if(commands[j]==201)paused=0;
                    else if(commands[j]==202)return 2;
                }
            }
            if(paused)continue;
        }
        if(at==used){
            size_t cap=sizeof(b)-(sizeof(b)%w->frame);
            used=0;at=0;
            if(w->decoder>0){
                /* Pipe reads may split frames: assemble complete frames before gain. */
                while(used<cap&&!eof&&!interrupted){
                    struct pollfd in={.fd=w->decoder_fd,.events=POLLIN};
                    int rc=poll(&in,1,100);
                    if(rc<0){if(errno==EINTR)continue;return 1;}
                    if(!rc){
                        if(controlled){
                            struct pollfd c={.fd=STDIN_FILENO,.events=POLLIN};
                            if(poll(&c,1,0)>0){
                                unsigned char cmd;ssize_t z=read(STDIN_FILENO,&cmd,1);
                                if(z<=0||cmd==202)return 2;
                                if(cmd<=100)volume=cmd;
                                else if(cmd==200)paused=1;
                                else if(cmd==201)paused=0;
                            }
                        }
                        if(paused&&used==0)break;
                        continue;
                    }
                    if(in.revents&(POLLERR|POLLNVAL))return 1;
                    ssize_t n=read(w->decoder_fd,b+used,cap-used);
                    if(n<0){if(errno==EAGAIN||errno==EINTR)continue;return 1;}
                    if(!n)eof=1;else used+=(size_t)n;
                }
                if(!used){if(eof)break;continue;}
                if(used%w->frame)return 1;
            } else {
                used=left<cap?left:cap;
                if(!readbytes(w->f,b,used))return 1;
                left-=used;
            }
            gain(b,used,w->bits,volume);
            if(paused)continue;
        }
        struct pollfd p={.fd=out,.events=POLLOUT};
        int rc=poll(&p,1,controlled?100:2000);
        if(rc<0){if(errno==EINTR)continue;return 1;}
        if(!rc){if(controlled)continue;return 1;}
        if(!(p.revents&POLLOUT))return 1;
        size_t n=used-at;if(n>4096)n=4096;
        ssize_t wrote=write(out,b+at,n);
        if(wrote<0){if(errno==EAGAIN||errno==EINTR)continue;return 1;}
        if(!wrote)return 1;
        at+=(size_t)wrote;
    }
    return interrupted?2:0;
}
static int playback(struct wav *w,int volume,int controlled){
    if(access("/usr/bin/aplay",X_OK)!=0){fputs("ALSA aplay unavailable\n",stderr);return 1;}
    int p[2];if(pipe(p)!=0)return 1;
    pid_t pid=fork();
    if(pid<0){close(p[0]);close(p[1]);return 1;}
    if(!pid){
        close(p[1]);if(dup2(p[0],STDIN_FILENO)<0)_exit(126);close(p[0]);
        char r[16],c[16];snprintf(r,sizeof(r),"%u",w->rate);snprintf(c,sizeof(c),"%u",w->channels);
        execl("/usr/bin/aplay","aplay","-q","-D","default","-t","raw","-f",w->bits==16?"S16_LE":"S24_3LE","-c",c,"-r",r,(char*)NULL);
        _exit(127);
    }
    close(p[0]);
    int flags=fcntl(p[1],F_GETFL);
    int rc=1;
    if(flags>=0&&fcntl(p[1],F_SETFL,flags|O_NONBLOCK)==0)rc=stream(w,p[1],volume,controlled);
    close(p[1]);
    if(rc)(void)kill(pid,SIGTERM);
    int status=0;pid_t got;
    do {got=waitpid(pid,&status,0);}while(got<0&&errno==EINTR);
    if(rc==2){stopped=1;return 0;}
    return rc||got!=pid||!WIFEXITED(status)||WEXITSTATUS(status)!=0;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS music v1");return 0;}
    if(argc<3||argc>5)return 2;
    const char *mode=argv[1];int volume=25,controlled=0;
    if(argc>=4){
        char *end;long v=strtol(argv[3],&end,10);
        if(end==argv[3]||*end||v<0||v>100)return 2;
        volume=(int)v;
    }
    if(argc==5){if(strcmp(argv[4],"--controlled"))return 2;controlled=1;}
    if(strcmp(mode,"--probe")&&strcmp(mode,"--decode")&&strcmp(mode,"--allow-play"))return 2;
    if(controlled&&strcmp(mode,"--allow-play"))return 2;
    struct wav w={.decoder_fd=-1};
    const char *format=demuxer(argv[2]);
    if(format){
        if(!strcmp(mode,"--probe")){puts("Compressed input; optional decoder produces stereo 48000 Hz S16LE");return access("/usr/bin/ffmpeg",X_OK)!=0;}
        if(!open_decoder(&w,argv[2],format))return 1;
    } else if(!parse(&w,argv[2])){if(w.f)fclose(w.f);fputs("Unsupported or malformed PCM WAV\n",stderr);return 1;}
    int rc=0;
    struct sigaction sa={.sa_handler=stop_signal};sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGTERM,&sa,NULL);(void)sigaction(SIGINT,&sa,NULL);
    (void)signal(SIGPIPE,SIG_IGN);
    if(!strcmp(mode,"--probe"))printf("rate=%u\nchannels=%u\nbits=%u\nframes=%u\n",w.rate,w.channels,w.bits,w.bytes/w.frame);
    else if(!strcmp(mode,"--decode"))rc=stream(&w,STDOUT_FILENO,argc==3?100:volume,0);
    else rc=playback(&w,volume,controlled);
    if(w.f)fclose(w.f);
    int decode_error=finish_decoder(&w,rc||stopped||interrupted);
    return rc||decode_error;
}
