/* Small, explicit metadata helper for PocketOS's ownership/permission guards. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--version")){puts("PocketOS file metadata v1");return 0;}
    if(argc!=4||strcmp(argv[1],"-c"))return 2;
    const char *format=argv[2];
    if(strcmp(format,"%u:%a")&&strcmp(format,"%u")&&strcmp(format,"%a")&&strcmp(format,"%s"))return 2;
    struct stat st;
    if(lstat(argv[3],&st)){perror("PocketOS metadata");return 1;}
    if(!strcmp(format,"%s")){if(!S_ISREG(st.st_mode)||st.st_size<0)return 1;printf("%jd\n",(intmax_t)st.st_size);}
    else if(!strcmp(format,"%u:%a"))printf("%ju:%o\n",(uintmax_t)st.st_uid,(unsigned)(st.st_mode&07777));
    else if(!strcmp(format,"%u"))printf("%ju\n",(uintmax_t)st.st_uid);
    else printf("%o\n",(unsigned)(st.st_mode&07777));
    return 0;
}
