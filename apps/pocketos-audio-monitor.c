/* Read-only Linux input headphone jack monitor for PocketOS.
 * Emits "wired=ready", "wired=absent", or "wired=unknown".
 * Never configures a mixer, radio, DAC, or output route.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_INPUTS 64
#define BIT_TEST(bits,bit) (((bits)[(bit)/(8*sizeof(unsigned long))]>>((bit)%(8*sizeof(unsigned long))))&1UL)
#define BIT_ARRAY(n) (((n)+(8*sizeof(unsigned long))-1)/(8*sizeof(unsigned long)))

struct jack { int fd; };
static struct jack jacks[MAX_INPUTS];

static void close_jacks(void) {
    for(int i=0;i<MAX_INPUTS;i++) { if(jacks[i].fd>=0)close(jacks[i].fd);jacks[i].fd=-1; }
}
static int initial(void) {
    DIR *d=opendir("/dev/input");
    if(!d)return -1;
    struct dirent *de;
    int count=0,present=0;
    while((de=readdir(d))&&count<MAX_INPUTS) {
        if(strncmp(de->d_name,"event",5))continue;
        const char *p=de->d_name+5;
        if(!*p||strspn(p,"0123456789")!=strlen(p))continue;
        char path[128];
        int len=snprintf(path,sizeof(path),"/dev/input/%s",de->d_name);
        if(len<=0||(size_t)len>=sizeof(path))continue;
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0)continue;
        unsigned long evbits[BIT_ARRAY(EV_MAX+1)]={0};
        unsigned long swbits[BIT_ARRAY(SW_MAX+1)]={0};
        unsigned long state[BIT_ARRAY(SW_MAX+1)]={0};
        if(ioctl(fd,EVIOCGBIT(0,sizeof(evbits)),evbits)<0||
           !BIT_TEST(evbits,EV_SW)||
           ioctl(fd,EVIOCGBIT(EV_SW,sizeof(swbits)),swbits)<0||
           !BIT_TEST(swbits,SW_HEADPHONE_INSERT)||
           ioctl(fd,EVIOCGSW(sizeof(state)),state)<0) { close(fd);continue; }
        present|=(int)BIT_TEST(state,SW_HEADPHONE_INSERT);
        jacks[count++].fd=fd;
    }
    closedir(d);
    if(!count)return -1;
    return present?1:0;
}
static void emit(int state) {
    puts(state<0?"wired=unknown":state?"wired=ready":"wired=absent");
    fflush(stdout);
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")) { puts("PocketOS audio monitor v1");return 0; }
    int watch=argc==2&&!strcmp(argv[1],"--watch");
    if(!watch&&(argc!=2||strcmp(argv[1],"--status")))return 2;
    for(int i=0;i<MAX_INPUTS;i++)jacks[i].fd=-1;
    int prior=initial();emit(prior);
    if(!watch){close_jacks();return 0;}
    for(;;){
        struct pollfd p[MAX_INPUTS];
        int n=0;
        for(int i=0;i<MAX_INPUTS;i++)if(jacks[i].fd>=0){
            p[n].fd=jacks[i].fd;p[n].events=POLLIN;p[n].revents=0;n++;
        }
        int rc=poll(p,(nfds_t)n,5000);
        if(rc<0){if(errno==EINTR)continue;break;}
        if(rc==0){ /* Detect a newly plugged input device without spinning. */
            close_jacks();
            int state=initial();
            if(state!=prior){prior=state;emit(state);}
            continue;
        }
        int changed=0;
        for(int i=0;i<n;i++){
            if(p[i].revents&(POLLHUP|POLLERR|POLLNVAL)){changed=1;continue;}
            if(!(p[i].revents&POLLIN))continue;
            struct input_event e[16];
            ssize_t got=read(p[i].fd,e,sizeof(e));
            if(got<=0){if(got==0||(errno!=EAGAIN&&errno!=EINTR))changed=1;continue;}
            if(got%(ssize_t)sizeof(e)!=0){changed=1;continue;}
            for(size_t j=0;j<(size_t)got/sizeof(e[0]);j++)
                if(e[j].type==EV_SW&&e[j].code==SW_HEADPHONE_INSERT)changed=1;
        }
        if(changed){
            close_jacks();
            int state=initial();
            if(state!=prior){prior=state;emit(state);}
        }
    }
    close_jacks();return 1;
}
