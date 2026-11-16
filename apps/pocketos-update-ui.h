/* One short-lived updater child, bounded line protocol, no arbitrary arguments. */
#ifndef POCKETOS_UPDATE_UI_H
#define POCKETOS_UPDATE_UI_H
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
enum update_job {U_NONE,U_STATUS,U_SD,U_GITHUB,U_ROLLBACK};
static enum update_job update_active,update_confirm;
static int update_allow,update_percent,update_fd=-1,update_len,update_drop;
static pid_t update_pid=-1;
static char update_line[192],update_note[80]="Select an update source";
static unsigned update_current,update_previous;
static struct timespec update_deadline;
static void update_message(const char *msg){snprintf(update_note,sizeof(update_note),"%.79s",msg);}
static void update_parse(const char *line){
    unsigned a,b;char extra;
    if(sscanf(line,"POCKETOS SELECTION %u %u %c",&a,&b,&extra)==2&&a<=999999999&&b<=999999999){
        update_current=a;update_previous=b;return;
    }
    int percent;
    if(sscanf(line,"POCKETOS PROGRESS %d %c",&percent,&extra)==1&&percent>=0&&percent<=100){
        if(percent>=update_percent)update_percent=percent;
        update_message(percent==100?"Verified; selection saved":percent>=90?"Saving verified selection":percent>=15?"Checking / transferring files":"Reading signed manifest");return;
    }
    if(!strncmp(line,"POCKETOS ERROR ",15)){
        update_message(line+15);return;
    }
}
static int update_drain(void){
    char chunk[512];ssize_t n;int changed=0;
    for(int i=0;i<16&&(n=read(update_fd,chunk,sizeof(chunk)))>0;i++){
        for(ssize_t j=0;j<n;j++){
            unsigned char c=(unsigned char)chunk[j];
            if(c=='\n'){
                if(!update_drop){update_line[update_len]=0;update_parse(update_line);changed=1;}
                update_drop=update_len=0;
            }else if(c>=32&&c<=126){
                if(update_len<(int)sizeof(update_line)-1)update_line[update_len++]=(char)c;
                else update_drop=1;
            }else update_drop=1;
        }
    }
    return changed;
}
static int update_spawn(enum update_job job){
    if(update_pid>=0)return 0;
    if(job!=U_STATUS&&!update_allow){update_message("Update actions locked");return 0;}
    const char *program="/usr/bin/pocketos-update";
    if(access(program,X_OK)!=0){update_message("Updater helper not installed");return 0;}
    int out[2];if(pipe(out)!=0){update_message("Cannot create updater pipe");return 0;}
    int flags=fcntl(out[0],F_GETFL);
    if(flags<0||fcntl(out[0],F_SETFL,flags|O_NONBLOCK)!=0||fcntl(out[0],F_SETFD,FD_CLOEXEC)!=0){
        close(out[0]);close(out[1]);update_message("Cannot prepare updater pipe");return 0;
    }
    pid_t pid=fork();
    if(pid==0){
        (void)setpgid(0,0);close(out[0]);
        if(dup2(out[1],STDOUT_FILENO)<0||dup2(out[1],STDERR_FILENO)<0)_exit(126);
        close(out[1]);
        int nullfd=open("/dev/null",O_RDONLY);
        if(nullfd<0||dup2(nullfd,STDIN_FILENO)<0)_exit(126);
        close(nullfd);
        if(job==U_SD)execl(program,program,"--ui","sd-inbox",(char*)NULL);
        else if(job==U_GITHUB)execl(program,program,"--ui","github","test-latest",(char*)NULL);
        else if(job==U_ROLLBACK)execl(program,program,"--ui","rollback",(char*)NULL);
        else execl(program,program,"--ui","status",(char*)NULL);
        _exit(127);
    }
    close(out[1]);if(pid<0){close(out[0]);update_message("Cannot start updater");return 0;}
    (void)setpgid(pid,pid);
    update_pid=pid;update_fd=out[0];update_active=job;
    update_len=update_drop=update_percent=0;
    if(clock_gettime(CLOCK_MONOTONIC,&update_deadline)!=0)update_deadline.tv_sec=0;
    else update_deadline.tv_sec+=1800;
    update_message(job==U_STATUS?"Reading selection":"Starting signed update");
    return 1;
}
static void update_close(void){
    if(update_pid>=0){
        /* Interrupt helper and its downloader; interrupted staging remains locked if cleanup fails. */
        (void)kill(-update_pid,SIGTERM);(void)kill(update_pid,SIGTERM);
        (void)kill(-update_pid,SIGKILL);(void)kill(update_pid,SIGKILL);
        (void)waitpid(update_pid,NULL,0);
    }
    if(update_fd>=0)close(update_fd);
    update_pid=-1;update_fd=-1;update_active=U_NONE;
}
static int update_poll(void){
    if(update_pid<0)return 0;
    int changed=update_drain(),status=0;
    pid_t p=waitpid(update_pid,&status,WNOHANG);
    if(p==0){
        struct timespec now;
        if(clock_gettime(CLOCK_MONOTONIC,&now)==0&&now.tv_sec<update_deadline.tv_sec)return changed;
        update_close();update_message("Timed out; inspect staged files");return 1;
    }
    if(p<0&&errno==EINTR)return changed;
    (void)update_drain();
    close(update_fd);update_fd=-1;update_pid=-1;
    enum update_job job=update_active;update_active=U_NONE;
    if(p<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0){
        if(!strcmp(update_note,"Starting signed update")||!strcmp(update_note,"Reading selection")||!strcmp(update_note,"Reading signed manifest")||!strcmp(update_note,"Checking / transferring files")||!strcmp(update_note,"Saving verified selection"))
            update_message("Update failed; selection not confirmed");
    }else if(job==U_STATUS)update_message(update_current?"Staged version selected":"No staged update");
    else{update_percent=100;update_message(job==U_ROLLBACK?"Previous package selected":"Verified package selected");}
    return 1;
}
#endif
