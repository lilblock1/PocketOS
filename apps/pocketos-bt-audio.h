/* Selected A2DP output; kernel connection query avoids a polling daemon. */
#ifndef POCKETOS_BT_AUDIO_H
#define POCKETOS_BT_AUDIO_H
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
/* Linux/BlueZ hci_conn_info and HCIGETCONNLIST UAPI, bounded to 16 links. */
struct pocket_hci_info {uint16_t handle;uint8_t address[6],type,out;uint16_t state;uint32_t mode;};
struct pocket_hci_list {uint16_t device,count;struct pocket_hci_info links[16];};
_Static_assert(sizeof(struct pocket_hci_info)==16,"HCI ABI size");
static int bt_audio_selected(char selected[18]){
    struct stat st;char text[32]={0};
    int fd=open("/run/pocketos-bluetooth/output",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return 0;
    int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&!(st.st_mode&022)&&st.st_size==18&&read(fd,text,sizeof(text)-1)==18;
    close(fd);
    if(!valid||text[17]!='\n')return 0;
    for(int i=0;i<17;i++)if(i%3==2?text[i]!=':':!isxdigit((unsigned char)text[i]))return 0;
    memcpy(selected,text,17);selected[17]=0;return 1;
}
static int bt_audio_ready(void){
    char text[18];unsigned octets[6];
    if(!bt_audio_selected(text)||sscanf(text,"%2x:%2x:%2x:%2x:%2x:%2x",&octets[0],&octets[1],&octets[2],&octets[3],&octets[4],&octets[5])!=6)return 0;
    uint8_t address[6];for(int i=0;i<6;i++)address[5-i]=(uint8_t)octets[i];
    int fd=socket(AF_BLUETOOTH,SOCK_RAW|SOCK_CLOEXEC,1);
    if(fd<0)return 0;
    struct pocket_hci_list list={.device=0,.count=16};
    int valid=ioctl(fd,_IOR('H',212,int),&list)==0&&list.count<=16;close(fd);
    if(valid)for(unsigned i=0;i<list.count;i++)
        if(list.links[i].type==1&&list.links[i].state==1&&!memcmp(list.links[i].address,address,6))return 1;
    return 0;
}
#endif
