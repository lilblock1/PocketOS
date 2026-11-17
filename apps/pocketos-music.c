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
#include "pocketos-audio-formats.h"
#include "pocketos-volume.h"
#include "pocketos-bt-audio.h"
static int bluetooth_output;

enum { AUDIO_NO_DECODER=10, AUDIO_BAD_FILE=11, AUDIO_NO_JACK=12, AUDIO_ROUTE=13, AUDIO_PCM=14 };

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
static void gain(unsigned char *p,size_t n,unsigned bits,int volume,int hardware){
    if(volume==100&&!hardware)return;
    if(volume==0){memset(p,0,n);return;}
    unsigned stride=bits/8,sign=1u<<(bits-1),mod=1u<<bits;
    for(size_t i=0;i<n;i+=stride){
        uint32_t raw=le(p+i,stride);
        int sample=raw&sign?(int)raw-(int)mod:(int)raw;
        /* Hardware UI 100 maps to the physically tested 15% decode gain.
         * Extended UI 130 maps to 19.5%; no DAC register writes. */
        int64_t product=(int64_t)sample*volume*volume;
        int32_t scaled=(int32_t)(hardware?product*225/100000000:product/10000);
        for(unsigned j=0;j<stride;j++)p[i+j]=(unsigned char)((uint32_t)scaled>>(8*j));
    }
}
static volatile sig_atomic_t interrupted=0;
static int stopped=0, progress_fd=-1;
static unsigned start_seconds=0;
static uint32_t total_seconds=0;
static void report_progress(uint32_t position){
    if(progress_fd<0)return;
    uint32_t msg[2]={position,total_seconds};
    ssize_t n=write(progress_fd,msg,sizeof(msg));(void)n;
}
static void stop_signal(int n){(void)n;interrupted=1;}
/* Only a launcher-verified root-owned RAM snapshot, or the trusted rootfs.
 * Never execute a path directly from the removable card. */
static const char *decoder_path(void){
    struct stat run,dir,exe;
    if(lstat("/run",&run)==0&&S_ISDIR(run.st_mode)&&run.st_uid==0&&!(run.st_mode&0022)&&
       lstat("/run/pocketos-decoder",&dir)==0&&S_ISDIR(dir.st_mode)&&dir.st_uid==0&&(dir.st_mode&0777)==0700&&
       lstat("/run/pocketos-decoder/ffmpeg",&exe)==0&&S_ISREG(exe.st_mode)&&exe.st_uid==0&&(exe.st_mode&0777)==0500&&
       exe.st_size>0&&exe.st_size<=12582912&&access("/run/pocketos-decoder/ffmpeg",X_OK)==0)
        return "/run/pocketos-decoder/ffmpeg";
    return access("/usr/bin/ffmpeg",X_OK)==0?"/usr/bin/ffmpeg":NULL;
}
/* Local file descriptors only; no URL input, shell, or network protocols. */
static int open_decoder(struct wav *w,const char *path,const char *format){
    const char *decoder=decoder_path();
    if(!decoder){fputs("Optional FFmpeg decoder unavailable\n",stderr);return 0;}
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
        char seek[16];snprintf(seek,sizeof(seek),"%u",start_seconds);
        execl(decoder,"ffmpeg","-nostdin","-hide_banner","-loglevel","error","-xerror",
              "-threads","1","-filter_threads","1","-max_alloc","16777216",
              "-probesize","262144","-analyzeduration","1000000",
              "-protocol_whitelist","file,pipe","-f",format,"-ss",seek,"-i","/proc/self/fd/3",
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
static int stream(struct wav *w,int out,int volume,int controlled,int hardware){
    unsigned char b[8192],incoming[4096],commands[64];
    size_t left=w->bytes,used=0,at=0;
    int paused=0,eof=0,decoded_any=0,extended=0;
    uint64_t sent=0;uint32_t last=start_seconds;
    report_progress(last);
    while(!interrupted&&(left||at<used||(w->decoder>0&&!eof))){
        if(controlled){
            struct pollfd c={.fd=STDIN_FILENO,.events=POLLIN};
            int pc=poll(&c,1,paused?5000:0);
            if(pc<0){if(errno==EINTR)continue;return 1;}
            if(pc>0){
                if(!(c.revents&POLLIN))return 2;
                ssize_t z=read(STDIN_FILENO,commands,sizeof(commands));
                if(z<=0)return 2;
                for(ssize_t j=0;j<z;j++){
                    if(commands[j]<=VOL_MAX||commands[j]==VOL_ENABLE||commands[j]==VOL_DISABLE)vol_command(commands[j],&volume,&extended);
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
                                if(cmd<=VOL_MAX||cmd==VOL_ENABLE||cmd==VOL_DISABLE)vol_command(cmd,&volume,&extended);
                                else if(cmd==200)paused=1;
                                else if(cmd==201)paused=0;
                            }
                        }
                        if(paused&&used==0)break;
                        continue;
                    }
                    if(in.revents&(POLLERR|POLLNVAL))return 1;
                    if(used>=sizeof(b)||cap>sizeof(b)||used>=cap)return 1;
                    size_t room=cap-used;
                    if(room>sizeof(incoming))room=sizeof(incoming);
                    ssize_t n=read(w->decoder_fd,incoming,room);
                    if(n<0){if(errno==EAGAIN||errno==EINTR)continue;return 1;}
                    if(!n)eof=1;
                    else {
                        if((size_t)n>room)return 1;
                        memcpy(b+used,incoming,(size_t)n);used+=(size_t)n;decoded_any=1;
                    }
                }
                if(!used){if(eof)break;continue;}
                if(used%w->frame)return 1;
            } else {
                used=left<cap?left:cap;
                if(!readbytes(w->f,b,used))return 1;
                left-=used;
            }
            gain(b,used,w->bits,volume,hardware);
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
        at+=(size_t)wrote;sent+=(size_t)wrote;
        uint32_t now=start_seconds+(uint32_t)(sent/((uint64_t)w->frame*w->rate));
        if(now!=last){report_progress(now);last=now;}
    }
    return interrupted?2:(w->decoder>0&&!decoded_any?1:0);
}
static int r1_mixer(const char *name,const char *value){
    pid_t child=fork();if(child<0)return 0;
    if(!child){execl("/usr/bin/amixer","amixer","-q","-c","0","cset",name,value,(char*)NULL);_exit(127);}
    int status=0;pid_t got;
    do{got=waitpid(child,&status,0);}while(got<0&&errno==EINTR);
    return got==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static int r1_headphones(void){
    FILE *f=fopen("/sys/class/switch/headset/state","r");
    if(!f)return 0;
    char state[8]={0};int ok=fgets(state,sizeof(state),f)!=NULL;
    fclose(f);
    return ok&&(!strcmp(state,"1\n")||!strcmp(state,"1"));
}
static int r1_audio_prepare(void){
    char id[64]={0};FILE *f=fopen("/sys/class/sound/card0/id","r");
    if(!f)return 0;
    int valid=fgets(id,sizeof(id),f)!=NULL;fclose(f);id[strcspn(id,"\r\n")]=0;
    if(!valid||strcmp(id,"hibysoundcard")||access("/usr/bin/amixer",X_OK))return 0;
    /* Match the audible-tested route. Do not write uncalibrated DAC volume
     * or mute controls. Require a freshly connected headset. */
    return r1_headphones()&&r1_mixer("numid=8","2")&&r1_headphones();
}
static int playback(struct wav *w,int volume,int controlled){
    if(bluetooth_output?!bt_audio_ready():!r1_headphones()){fputs(bluetooth_output?"Connect Bluetooth audio before playback\n":"Connect headphones before playback\n",stderr);return AUDIO_NO_JACK;}
    if(access("/usr/bin/aplay",X_OK)!=0){fputs("ALSA aplay unavailable\n",stderr);return AUDIO_PCM;}
    if(!bluetooth_output&&!r1_audio_prepare()){fputs("R1 headphone route initialization failed\n",stderr);return AUDIO_ROUTE;}
    int p[2];if(pipe(p)!=0)return 1;
    pid_t pid=fork();
    if(pid<0){close(p[0]);close(p[1]);return 1;}
    if(!pid){
        close(p[1]);if(dup2(p[0],STDIN_FILENO)<0)_exit(126);close(p[0]);
        if(bluetooth_output&&setenv("ALSA_CONFIG_PATH","/run/pocketos-bluetooth/asound.conf",1))_exit(126);
        char r[16],c[16];snprintf(r,sizeof(r),"%u",w->rate);snprintf(c,sizeof(c),"%u",w->channels);
        if(bluetooth_output&&setenv("ALSA_CONFIG_PATH","/run/pocketos-bluetooth/asound.conf",1))_exit(126);
        execl("/usr/bin/aplay","aplay","-q","-D",bluetooth_output?"pocketos_bluetooth":w->rate==48000&&w->channels==2&&w->bits==16?"hw:0,0":"plughw:CARD=hibysoundcard,DEV=0","-t","raw","-f",w->bits==16?"S16_LE":"S24_3LE","-c",c,"-r",r,(char*)NULL);
        _exit(127);
    }
    close(p[0]);
    int flags=fcntl(p[1],F_GETFL);
    int rc=1;
    if(flags>=0&&fcntl(p[1],F_SETFL,flags|O_NONBLOCK)==0)rc=stream(w,p[1],volume,controlled,1);
    close(p[1]);
    if(rc)(void)kill(pid,SIGTERM);
    int status=0;pid_t got;
    do {got=waitpid(pid,&status,0);}while(got<0&&errno==EINTR);
    if(rc==2){stopped=1;return 0;}
    if(rc||got!=pid||!WIFEXITED(status)||WEXITSTATUS(status)!=0){fputs("ALSA PCM playback failed\n",stderr);return AUDIO_PCM;}
    return 0;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--test-bt-audio")){printf("ready=%d\n",bt_audio_ready());return 0;}
    if(argc==2&&!strcmp(argv[1],"--test-playback-gain")){
        unsigned char a[]={0,128,255,127}, b[]={0,128,255,127}, c[]={0,128,255,127};
        gain(a,sizeof(a),16,100,1);gain(b,sizeof(b),16,15,0);gain(c,sizeof(c),16,0,1);
        if(memcmp(a,b,sizeof(a))||memcmp(c,"\0\0\0\0",sizeof(c)))return 1;
        int v=100,e=0;vol_command(130,&v,&e);if(v!=100||e)return 2;
        vol_command(VOL_ENABLE,&v,&e);if(v!=100||!e)return 3;
        vol_command(130,&v,&e);if(v!=130)return 4;
        unsigned char high[]={0,128,255,127},hi24[]={0,0,128,255,255,127};
        gain(high,sizeof(high),16,v,1);gain(hi24,sizeof(hi24),24,v,1);
        if((int16_t)le(high+2,2)!=(int64_t)32767*38025/1000000||
           (int16_t)le(high,2)!=(int64_t)-32768*38025/1000000||
           le(hi24+3,3)!=(int64_t)8388607*38025/1000000)return 5;
        vol_command(VOL_DISABLE,&v,&e);if(v!=100||e)return 6;
        vol_command(130,&v,&e);return v!=100;
    }
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS music v1");return 0;}
    if(argc<3||argc>10)return 2;
    const char *mode=argv[1];int volume=25,controlled=0;
    if(argc>=4){
        char *end;long v=strtol(argv[3],&end,10);
        if(end==argv[3]||*end||v<0||v>100)return 2;
        volume=(int)v;
    }
    for(int i=4;i<argc;i++){
        if(!strcmp(argv[i],"--controlled"))controlled=1;
        else if(!strcmp(argv[i],"--bluetooth"))bluetooth_output=1;
        else if(!strcmp(argv[i],"--start")&&i+1<argc){
            char *end;long v=strtol(argv[++i],&end,10);
            if(end==argv[i]||*end||v<0||v>604800)return 2;
            start_seconds=(unsigned)v;
        } else if(!strcmp(argv[i],"--progress-fd")&&i+1<argc){
            char *end;long v=strtol(argv[++i],&end,10);
            if(end==argv[i]||*end||v<3||v>1024)return 2;
            progress_fd=(int)v;
            int flags=fcntl(progress_fd,F_GETFL);
            if(flags<0||fcntl(progress_fd,F_SETFL,flags|O_NONBLOCK)!=0||fcntl(progress_fd,F_SETFD,FD_CLOEXEC)!=0)return 2;
        } else return 2;
    }
    if(strcmp(mode,"--probe")&&strcmp(mode,"--decode")&&strcmp(mode,"--allow-play"))return 2;
    if(controlled&&strcmp(mode,"--allow-play"))return 2;
    if(bluetooth_output&&strcmp(mode,"--allow-play"))return 2;
    if(!strcmp(mode,"--allow-play")){
        if(bluetooth_output?!bt_audio_ready():!r1_headphones()){fputs(bluetooth_output?"Connect Bluetooth audio before playback\n":"Connect headphones before playback\n",stderr);return AUDIO_NO_JACK;}
    }
    struct wav w={.decoder_fd=-1};
    const char *format=pocketos_demuxer(argv[2]);
    int native_wav=(!format||!strcmp(format,"wav"))&&parse(&w,argv[2]);
    if(!native_wav&&w.f){fclose(w.f);w.f=NULL;}
    if(!native_wav&&format&&strcmp(format,"wav")){
        if(!strcmp(mode,"--probe")){puts("Compressed input; optional decoder produces stereo 48000 Hz S16LE");return decoder_path()==NULL;}
        if(!decoder_path()){fputs("Optional FFmpeg decoder unavailable\n",stderr);return AUDIO_NO_DECODER;}
        if(!open_decoder(&w,argv[2],format))return AUDIO_BAD_FILE;
    } else if(!native_wav){fputs("Unsupported audio file\n",stderr);return AUDIO_BAD_FILE;}
    if(native_wav){
        total_seconds=(uint32_t)((uint64_t)w.bytes/(w.rate*w.frame));
        uint64_t skip=(uint64_t)start_seconds*w.rate*w.frame;
        if(start_seconds&&((uint64_t)w.bytes<=skip||fseeko(w.f,w.offset+(off_t)skip,SEEK_SET)!=0)){fclose(w.f);return 1;}
        w.bytes-=(uint32_t)skip;
    }
    int rc=0;
    struct sigaction sa={.sa_handler=stop_signal};sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGTERM,&sa,NULL);(void)sigaction(SIGINT,&sa,NULL);
    (void)signal(SIGPIPE,SIG_IGN);
    if(!strcmp(mode,"--probe"))printf("rate=%u\nchannels=%u\nbits=%u\nframes=%u\n",w.rate,w.channels,w.bits,w.bytes/w.frame);
    else if(!strcmp(mode,"--decode"))rc=stream(&w,STDOUT_FILENO,argc==3?100:volume,0,0);
    else rc=playback(&w,volume,controlled);
    if(w.f)fclose(w.f);
    int decode_error=finish_decoder(&w,rc||stopped||interrupted);
    return rc?rc:decode_error?AUDIO_BAD_FILE:0;
}
