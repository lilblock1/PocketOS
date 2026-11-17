/* HOST QEMU FIXTURE ONLY. Never install or preload on the R1.
 * Executes the digest-pinned recovery ELF's actual monitor control flow.
 * Replaces display, file predicates and command boundaries in process memory;
 * no script, NAND operation, boot flag write or reboot is executed here.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/cachectl.h>
#include <unistd.h>

static int full, kernel_waits, normal, finish;
static int ui(int a,int b){(void)a;(void)b;return 0;}
static int stage(int n){if(n==5)finish=1;return 0;}
static int exists(const char *s){
    if(!strcmp(s,"/tmp/ota/ota_site_version.ok"))return 1;
    if(!strcmp(s,"/tmp/ota/rootfs.squashfs.ok"))return 1;
    if(!strcmp(s,"/tmp/ota/xImage.ok")){
        if(full)return 1;
        if(++kernel_waits==64){
            puts("MONITOR-STUCK: rootfs complete, missing xImage.ok, no normal-boot request");
            fflush(stdout);_exit(42);
        }
    }
    return 0;
}
int system(const char *s){
    if(!strcmp(s,"/usr/bin/bootmode.sh Recovery"))return 0;
    if(!strcmp(s,"/usr/bin/bootmode.sh Normal")){normal++;return 0;}
    _exit(91);
}
FILE *popen(const char *s,const char *mode){
    static char zero[]="0\n", six[]="6\n", count[]="25\n", empty[]="fixture\n";
    (void)mode;
    char *v;
    if(strstr(s,"xImage.00* | wc -l"))v=full?six:zero;
    else if(strstr(s,"rootfs.squashfs.00* | wc -l"))v=count;
    else if(!strcmp(s,"/etc/ota_bin/local_ota_update.sh /data/autoupdate/autoupdate"))v=empty;
    else _exit(92);
    return fmemopen(v,strlen(v),"r");
}
int pclose(FILE *f){return fclose(f);}
FILE *fopen(const char *s,const char *mode){
    static char config[]="current_version=0\n";
    (void)mode;
    if(strcmp(s,"/data/autoupdate/autoupdate/ota_config.in"))_exit(93);
    return fmemopen(config,strlen(config),"r");
}
int reboot(int cmd){(void)cmd;_exit(94);}
static void patch(uintptr_t address,void *target){
    uintptr_t p=(uintptr_t)target;
    if(mprotect((void*)(address&~4095u),4096,PROT_READ|PROT_WRITE|PROT_EXEC))_exit(95);
    uint32_t jump[]={0x3c190000u|(uint32_t)(p>>16),0x37390000u|(uint32_t)(p&65535),0x03200008u,0};
    memcpy((void*)address,jump,sizeof(jump));
    if(cacheflush((void*)address,sizeof(jump),BCACHE))_exit(96);
}
__attribute__((constructor)) static void run_fixture(void){
    struct utsname u;
    const char *gate=getenv("POCKETOS_MONITOR_FIXTURE"),*kind=getenv("POCKETOS_MONITOR_KIND");
    if(!gate||strcmp(gate,"host-qemu-only")||!kind||uname(&u)||!strcmp(u.release,"4.4.94-sonix"))_exit(97);
    full=!strcmp(kind,"kernel-and-rootfs");
    patch(0x404ab0,(void*)ui);patch(0x4058fc,(void*)ui);
    patch(0x405b48,(void*)stage);patch(0x406048,(void*)exists);
    ((int(*)(void))0x40610c)();
    if(!full||normal!=1||!finish)_exit(98);
    puts("MONITOR-COMPLETED: finish state and one normal-boot request");
    fflush(stdout);_exit(0);
}
