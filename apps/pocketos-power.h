/* PocketOS opt-in display idle and CPU policy. No suspend or register writes. */
#ifndef POCKETOS_POWER_H
#define POCKETOS_POWER_H
#include <sys/vfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int power_allow, power_blank, power_cpu_allow;
static int power_stage, power_failed, cpu_changed, cpu_attempted;
static long power_saved=-1,power_written=-1;
static char power_dir[1024],cpu_dir[1024],cpu_original[32],cpu_target[32];
static long ram_total=-1,ram_available=-1,shell_rss=-1;
static long long mono_ms(void){
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int idle_target(long long seconds,int saving,int reader,int busy,int blank){
    if(busy)return 0;
    long dim=reader?(saving?90:180):(saving?25:60);
    long off=reader?(saving?180:300):(saving?60:120);
    if(blank&&seconds>=off)return 2;
    return seconds>=dim?1:0;
}
static int limit_wait(int timeout,long long now,long long deadline){
    if(now<0||deadline<0)return timeout;
    long long left=deadline-now;
    if(left<1)return 1;
    return left<timeout?(int)left:timeout;
}
static long read_kib(const char *path,const char *key){
    FILE *f=fopen(path,"r");if(!f)return -1;
    char line[128];long result=-1;
    while(fgets(line,sizeof(line),f)){
        size_t n=strlen(key);
        if(!strncmp(line,key,n)){
            long v;char unit[8];
            if(sscanf(line+n,"%ld %7s",&v,unit)==2&&v>=0&&!strcmp(unit,"kB"))result=v;
            break;
        }
    }
    fclose(f);return result;
}
static void power_memory(void){
    ram_total=read_kib("/proc/meminfo","MemTotal:");
    ram_available=read_kib("/proc/meminfo","MemAvailable:");
    shell_rss=read_kib("/proc/self/status","VmRSS:");
}
static int power_sysfs(const char *dir){
    struct statfs fs;return statfs(dir,&fs)==0&&(unsigned long)fs.f_type==0x62656572UL;
}
static int power_attr_read(const char *dir,const char *name,char *out,size_t len){
    char path[1200];int n=snprintf(path,sizeof(path),"%s/%s",dir,name);
    if(n<=0||(size_t)n>=sizeof(path))return 0;
    FILE *f=fopen(path,"r");if(!f)return 0;
    int ok=fgets(out,(int)len,f)!=NULL;fclose(f);
    if(!ok)return 0;
    size_t end=strcspn(out,"\r\n");if(end==len-1)return 0;
    out[end]=0;return end>0;
}
static int power_attr_write(const char *dir,const char *name,const char *value){
    if(!power_sysfs(dir))return 0;
    char path[1200],buf[64];int n=snprintf(path,sizeof(path),"%s/%s",dir,name);
    if(n<=0||(size_t)n>=sizeof(path))return 0;
    n=snprintf(buf,sizeof(buf),"%s\n",value);
    if(n<=0||(size_t)n>=sizeof(buf))return 0;
    int fd=open(path,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return 0;
    int ok=write(fd,buf,(size_t)n)==n;
    if(close(fd)!=0)ok=0;
    return ok;
}
static int governor_listed(const char *list,const char *name){
    size_t n=strlen(name);
    while(*list){
        while(*list==' '||*list=='\t')list++;
        size_t k=strcspn(list," \t");if(k==n&&!memcmp(list,name,n))return 1;
        list+=k;
    }
    return 0;
}
/* Only a single advertised CPU policy, and only a performance -> adaptive change.
 * Avoid forcing frequency limits, voltages, unsupported governors or powersave. */
static int cpu_restore(void){
    if(!cpu_changed)return 1;
    char actual[32];
    if(!power_attr_read(cpu_dir,"scaling_governor",actual,sizeof(actual)))return 0;
    if(strcmp(actual,cpu_target)){cpu_changed=0;return 1;} /* external owner took over */
    if(!power_attr_write(cpu_dir,"scaling_governor",cpu_original)||
       !power_attr_read(cpu_dir,"scaling_governor",actual,sizeof(actual))||strcmp(actual,cpu_original))return 0;
    cpu_changed=0;return 1;
}
static void cpu_policy(int saving){
    if(!power_cpu_allow)return;
    if(!saving){if(cpu_restore())cpu_attempted=0;return;}
    if(cpu_attempted)return;
    cpu_attempted=1;
    glob_t g={0};
    if(glob("/sys/devices/system/cpu/cpufreq/policy*",0,NULL,&g)==0&&g.gl_pathc==1&&strlen(g.gl_pathv[0])<sizeof(cpu_dir))
        strcpy(cpu_dir,g.gl_pathv[0]);
    else cpu_dir[0]=0;
    globfree(&g);
    if(!cpu_dir[0]||!power_sysfs(cpu_dir))return;
    char list[256],actual[32];
    if(!power_attr_read(cpu_dir,"scaling_governor",cpu_original,sizeof(cpu_original))||strcmp(cpu_original,"performance")||
       !power_attr_read(cpu_dir,"scaling_available_governors",list,sizeof(list)))return;
    const char *candidate=governor_listed(list,"schedutil")?"schedutil":governor_listed(list,"ondemand")?"ondemand":NULL;
    if(!candidate)return;
    strcpy(cpu_target,candidate);cpu_changed=1;
    if(!power_attr_write(cpu_dir,"scaling_governor",candidate)||
       !power_attr_read(cpu_dir,"scaling_governor",actual,sizeof(actual))||strcmp(actual,candidate))(void)cpu_restore();
}
#endif
