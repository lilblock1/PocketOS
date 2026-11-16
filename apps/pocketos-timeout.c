/* Bounded child runner for minimal R1 userspace; no shell or setuid. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static long long ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static volatile sig_atomic_t stopping;
static void stop(int sig){stopping=sig;}
int main(int argc,char **argv){
    if(argc<3)return 2;
    char *end;long seconds=strtol(argv[1],&end,10);
    if(end==argv[1]||*end||seconds<1||seconds>60)return 2;
    long long began=ms();if(began<0)return 125;
    struct sigaction action={.sa_handler=stop};sigemptyset(&action.sa_mask);
    (void)sigaction(SIGTERM,&action,NULL);(void)sigaction(SIGINT,&action,NULL);(void)sigaction(SIGHUP,&action,NULL);
    pid_t child=fork();if(child<0)return 125;
    if(!child){
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
        if(setpgid(0,0))_exit(125);
        execvp(argv[2],argv+2);_exit(127);
    }
    (void)setpgid(child,child);
    int expired=0,status=0;
    for(;;){
        pid_t got=waitpid(child,&status,WNOHANG);
        if(got==child){if(expired||stopping)(void)kill(-child,SIGKILL);break;}
        if(got<0&&errno!=EINTR)return 125;
        long long now=ms();
        if(stopping||now<0||now-began>=seconds*1000){
            if(!expired){(void)kill(-child,SIGTERM);expired=1;}
            if(stopping||now<0||now-began>=seconds*1000+500){(void)kill(-child,SIGKILL);}
        }
        struct timespec pause={.tv_nsec=50000000};(void)nanosleep(&pause,NULL);
    }
    return stopping?128+stopping:expired?124:WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
