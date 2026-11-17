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
enum update_job {U_NONE,U_STATUS,U_SD,U_GITHUB,U_ROLLBACK,U_STABLE,U_CHECK_STABLE,U_CHECK_TEST,U_INSTALL};
static enum update_job update_active,update_confirm;
static int update_allow,update_percent,update_fd=-1,update_len,update_drop;
static pid_t update_pid=-1;
static char update_line[192],update_note[80]="Select an update source";
static unsigned update_current,update_previous,update_bytes,update_total,update_offer;
static int update_error,update_auto,update_handoff;
static unsigned update_auto_seq;
static char update_auto_channel[7];
static unsigned update_ready_seq;
static char update_ready_channel[7];
static struct timespec update_deadline;
static void update_message(const char *msg){snprintf(update_note,sizeof(update_note),"%.79s",msg);}
static void update_parse(const char *line){
    unsigned a,b;char extra;
    if(sscanf(line,"POCKETOS SELECTION %u %u %c",&a,&b,&extra)==2&&a<=999999999&&b<=999999999){
        update_current=a;update_previous=b;return;
    }
    if(sscanf(line,"POCKETOS AVAILABLE %u %c",&a,&extra)==1&&a<=999999999){
        if(update_active==U_CHECK_STABLE||update_active==U_CHECK_TEST)update_offer=a;
        return;
    }
    char channel[7];
    if(sscanf(line,"POCKETOS READY %u %6s %c",&a,channel,&extra)==2&&a>0&&a<=999999999&&
       (!strcmp(channel,"stable")||!strcmp(channel,"test"))){
        if((update_active==U_SD||update_active==U_STABLE||update_active==U_GITHUB||
            update_active==U_CHECK_STABLE||update_active==U_CHECK_TEST)&&
           (update_active==U_SD||
            ((!strcmp(channel,"stable"))==(update_active==U_STABLE||update_active==U_CHECK_STABLE)))){
            update_ready_seq=a;snprintf(update_ready_channel,sizeof(update_ready_channel),"%s",channel);
        }
        return;
    }
    unsigned done,total;
    if(sscanf(line,"POCKETOS BYTES %u %u %c",&done,&total,&extra)==2&&total>0&&total<=536870912&&done<=total){
        if((!update_total||update_total==total)&&done>=update_bytes){update_bytes=done;update_total=total;}
        return;
    }
    if(!strncmp(line,"POCKETOS PHASE ",15)){
        const char *p=line+15;
        if(!strcmp(p,"MANIFEST"))update_message("Checking signed manifest");
        else if(!strcmp(p,"DOWNLOAD"))update_message("Downloading to microSD");
        else if(!strcmp(p,"COPY"))update_message("Copying to microSD");
        else if(!strcmp(p,"VERIFY"))update_message("Verifying package checksums");
        else if(!strcmp(p,"SAVE"))update_message("Saving verified selection");
        else if(!strcmp(p,"PREFLIGHT"))update_message("Checking battery and recovery");
        else if(!strcmp(p,"FLUSH"))update_message("Saving and unmounting microSD");
        else if(!strcmp(p,"RECOVERY")){update_handoff=1;update_message("Preparing recovery boot");}
        else if(!strcmp(p,"REBOOT")){update_handoff=1;update_message("Restarting into recovery");}
        else if(!strcmp(p,"DONE"))update_message("Staging finished; not installed");
        return;
    }
    int percent;
    if(sscanf(line,"POCKETOS PROGRESS %d %c",&percent,&extra)==1&&percent>=0&&percent<=100){
        if(percent>=update_percent)update_percent=percent;
        return;
    }
    if(!strncmp(line,"POCKETOS ERROR ",15)){
        update_error=1;update_message(line+15);return;
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
static int update_display_percent(void){
    if(update_active==U_INSTALL)return 70+update_percent*29/100;
    if(update_active==U_GITHUB||update_active==U_STABLE||update_active==U_SD)
        return update_percent*70/100;
    return update_percent;
}
static int update_auto_matches(void){
    return update_auto&&update_ready_seq&&update_ready_seq==update_current&&
        update_ready_seq==update_auto_seq&&!strcmp(update_ready_channel,update_auto_channel);
}
static void update_clear_auto(void){update_auto=0;update_auto_seq=0;update_auto_channel[0]=0;}
static int update_spawn(enum update_job job){
    if(update_pid>=0||update_handoff)return 0;
    if(job==U_INSTALL&&(!update_ready_seq||update_ready_seq!=update_current||
       (strcmp(update_ready_channel,"stable")&&strcmp(update_ready_channel,"test")))){
        update_message("Recheck the update source");return 0;
    }
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
        else if(job==U_CHECK_STABLE||job==U_CHECK_TEST)
            execl(program,program,"--ui","check-github",job==U_CHECK_STABLE?"stable-latest":"test-latest",(char*)NULL);
        else if(job==U_STABLE||job==U_GITHUB){
            char seq[16];snprintf(seq,sizeof(seq),"%u",update_offer);
            execl(program,program,"--ui","github",job==U_STABLE?"stable-latest":"test-latest",seq,(char*)NULL);
        }
        else if(job==U_INSTALL){
            char seq[16];snprintf(seq,sizeof(seq),"%u",update_ready_seq);
            execl(program,program,"--ui","install",seq,update_ready_channel,(char*)NULL);
        }
        else if(job==U_ROLLBACK)execl(program,program,"--ui","rollback",(char*)NULL);
        else execl(program,program,"--ui","status",(char*)NULL);
        _exit(127);
    }
    close(out[1]);if(pid<0){close(out[0]);update_message("Cannot start updater");return 0;}
    (void)setpgid(pid,pid);
    update_pid=pid;update_fd=out[0];update_active=job;
    update_len=update_drop=update_percent=update_error=0;update_bytes=update_total=0;
    if(job!=U_INSTALL){update_ready_seq=0;update_ready_channel[0]=0;}
    if(job==U_CHECK_STABLE||job==U_CHECK_TEST){update_offer=0;update_clear_auto();}
    if(clock_gettime(CLOCK_MONOTONIC,&update_deadline)!=0)update_deadline.tv_sec=0;
    else update_deadline.tv_sec+=1800;
    update_message(job==U_STATUS?"Reading selection":"Starting signed update");
    return 1;
}
static int update_accept(enum update_job job){
    if(job==U_STABLE||job==U_GITHUB){
        update_auto=1;update_auto_seq=update_offer;
        snprintf(update_auto_channel,sizeof(update_auto_channel),"%s",job==U_STABLE?"stable":"test");
    }else update_clear_auto();
    if(update_spawn(job))return 1;
    update_clear_auto();return 0;
}
static void update_close(void){
    if(update_pid>=0){
        /* An install child must not be killed while the boot flag is being written. */
        if(update_active==U_INSTALL)return;
        /* Interrupt helper and its downloader; interrupted staging remains locked if cleanup fails. */
        (void)kill(-update_pid,SIGTERM);(void)kill(update_pid,SIGTERM);
        /* Allow shell EXIT traps to remove the staging and runtime locks. */
        int status;pid_t got=0;
        for(int i=0;i<30&&got==0;i++){
            got=waitpid(update_pid,&status,WNOHANG);
            if(got<0&&errno==EINTR)got=0;
            if(got==0){struct timespec delay={.tv_nsec=100000000L};(void)nanosleep(&delay,NULL);}
        }
        if(got==0){
            (void)kill(-update_pid,SIGKILL);(void)kill(update_pid,SIGKILL);
            while(waitpid(update_pid,NULL,0)<0&&errno==EINTR){}
        }
    }
    if(update_fd>=0)close(update_fd);
    update_pid=-1;update_fd=-1;update_active=U_NONE;update_clear_auto();
}
static int update_poll(void){
    if(update_pid<0)return 0;
    int changed=update_drain(),status=0;
    pid_t p=waitpid(update_pid,&status,WNOHANG);
    if(p==0){
        if(update_active==U_INSTALL)return changed;
        struct timespec now;
        if(clock_gettime(CLOCK_MONOTONIC,&now)==0&&now.tv_sec<update_deadline.tv_sec)return changed;
        update_close();update_message("Timed out; inspect staged files");return 1;
    }
    if(p<0&&errno==EINTR)return changed;
    (void)update_drain();
    close(update_fd);update_fd=-1;update_pid=-1;
    enum update_job job=update_active;update_active=U_NONE;
    if(p<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0){
        update_ready_seq=0;update_ready_channel[0]=0;update_clear_auto();
        if(!update_error)
            update_message("Update failed; selection not confirmed");
    }else if(job==U_STATUS)update_message(update_current?"Staged version selected":"No staged update");
    else if(update_ready_seq&&update_ready_seq==update_current&&
            (job==U_SD||job==U_STABLE||job==U_GITHUB||job==U_CHECK_STABLE||job==U_CHECK_TEST)){
        if((job==U_GITHUB||job==U_STABLE)&&update_auto_matches()){
            update_clear_auto();
            if(update_spawn(U_INSTALL))return 1;
        }
        update_clear_auto();
        update_percent=100;update_confirm=U_INSTALL;update_message("Verified firmware ready to install");
    }
    else if(job==U_CHECK_STABLE||job==U_CHECK_TEST){
        if(update_offer){update_confirm=job==U_CHECK_STABLE?U_STABLE:U_GITHUB;update_message("Signed release available");}
        else update_message("No newer release than staged history");
    }
    else{update_percent=100;update_message(job==U_ROLLBACK?"Previous package selected":"Verified package staged; not installed");}
    return 1;
}
#endif
