/* On-demand PocketOS transfer server supervisor. Included by pocketos-shell.c.
 * User starts and stops service explicitly. No process or polling when disabled.
 */
#ifndef POCKETOS_TRANSFER_UI_H
#define POCKETOS_TRANSFER_UI_H
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

static int transfer_allow=0;
static pid_t transfer_pid=-1;
static int transfer_fd=-1,transfer_used=0;
static char transfer_buffer[160],transfer_url[64],transfer_pin[16];
static char transfer_note[52]="Off (no RAM used by server)";
static void transfer_note_set(const char *m){
    snprintf(transfer_note,sizeof(transfer_note),"%.51s",m);
}
static int transfer_start(void){
    if(transfer_pid>0)return 0;
    if(!transfer_allow){
        transfer_note_set("Transfer actions locked");return 0;
    }
    if(access("/usr/bin/pocketos-transfer",X_OK)!=0){
        transfer_note_set("Transfer server not installed");return 0;
    }
    int p[2];
    if(pipe(p)!=0){transfer_note_set("Cannot start transfer");return 0;}
    int flags=fcntl(p[0],F_GETFL);
    if(flags<0||fcntl(p[0],F_SETFL,flags|O_NONBLOCK)!=0||
       fcntl(p[0],F_SETFD,FD_CLOEXEC)!=0){
        close(p[0]);close(p[1]);transfer_note_set("Cannot monitor transfer");return 0;
    }
    pid_t child=fork();
    if(child==0){
        close(p[0]);
        if(dup2(p[1],STDOUT_FILENO)<0)_exit(126);
        close(p[1]);
        int nullfd=open("/dev/null",O_RDONLY|O_CLOEXEC);
        if(nullfd<0||dup2(nullfd,STDIN_FILENO)<0)_exit(126);
        close(nullfd);
        nullfd=open("/dev/null",O_WRONLY|O_CLOEXEC);
        if(nullfd<0||dup2(nullfd,STDERR_FILENO)<0)_exit(126);
        close(nullfd);
        execl("/usr/bin/pocketos-transfer","pocketos-transfer",(char*)NULL);
        _exit(127);
    }
    close(p[1]);
    if(child<0){close(p[0]);transfer_note_set("Cannot start transfer");return 0;}
    transfer_pid=child;transfer_fd=p[0];transfer_used=0;
    transfer_url[0]=transfer_pin[0]=0;
    transfer_note_set("Starting Wi-Fi transfer...");
    return 1;
}
static int transfer_poll(void){
    if(transfer_pid<0)return 0;
    int changed=0;
    if(transfer_fd>=0){
        while(transfer_used<(int)sizeof(transfer_buffer)-1){
            ssize_t got=read(transfer_fd,transfer_buffer+transfer_used,
                             sizeof(transfer_buffer)-1-(size_t)transfer_used);
            if(got>0){transfer_used+=(int)got;changed=1;continue;}
            if(got==0){close(transfer_fd);transfer_fd=-1;break;}
            if(errno==EAGAIN||errno==EINTR)break;
            close(transfer_fd);transfer_fd=-1;break;
        }
        transfer_buffer[transfer_used]=0;
        char *sep=strchr(transfer_buffer,'\n');
        if(sep){
            char *end=strchr(sep+1,'\n');
            size_t url_len=(size_t)(sep-transfer_buffer);
            if(end&&url_len>7&&url_len<sizeof(transfer_url)&&
               !strncmp(transfer_buffer,"http://",7)&&end-(sep+1)==8){
                int digits=1;
                for(char *p=sep+1;p<end;p++)if(!isdigit((unsigned char)*p))digits=0;
                if(digits){
                    memcpy(transfer_url,transfer_buffer,url_len);transfer_url[url_len]=0;
                    memcpy(transfer_pin,sep+1,8);transfer_pin[8]=0;
                    transfer_note_set("Use a browser on the same Wi-Fi");
                    close(transfer_fd);transfer_fd=-1;
                    changed=1;
                }
            }
        }
        if(transfer_used==(int)sizeof(transfer_buffer)-1&&transfer_fd>=0){
            close(transfer_fd);transfer_fd=-1;
            transfer_note_set("Invalid server response");changed=1;
        }
    }
    int status=0;
    pid_t reaped=waitpid(transfer_pid,&status,WNOHANG);
    if(reaped==transfer_pid||(reaped<0&&errno==ECHILD)){
        transfer_pid=-1;
        if(transfer_fd>=0)close(transfer_fd);
        transfer_fd=-1;transfer_used=0;
        transfer_pin[0]=transfer_url[0]=0;
        transfer_note_set("Stopped or timed out");changed=1;
    }
    return changed;
}
static void transfer_stop(void){
    if(transfer_pid>0){
        (void)kill(transfer_pid,SIGTERM);
        transfer_note_set("Stopping transfer...");
    }
}
static void transfer_cleanup(void){
    if(transfer_pid>0){
        (void)kill(transfer_pid,SIGTERM);
        /* Let the server unlink an interrupted partial upload. */
        for(int i=0;i<30&&transfer_pid>0;i++){
            int status=0;
            pid_t got=waitpid(transfer_pid,&status,WNOHANG);
            if(got==transfer_pid||(got<0&&errno==ECHILD)){transfer_pid=-1;break;}
            struct timespec delay={.tv_sec=0,.tv_nsec=100000000};
            (void)nanosleep(&delay,NULL);
        }
        if(transfer_pid>0){
            (void)kill(transfer_pid,SIGKILL);
            (void)waitpid(transfer_pid,NULL,0);
        }
    }
    transfer_pid=-1;
    if(transfer_fd>=0)close(transfer_fd);
    transfer_fd=-1;
    transfer_pin[0]=transfer_url[0]=0;
}
#endif
