#define _GNU_SOURCE
#include "native_types.h"
#include "resources.h"
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <inttypes.h>
#include <link.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static uintptr_t vendor_base;
static void *original_main_address;
static int report_fd=STDOUT_FILENO;

static void fatal(const char *reason)
{
    dprintf(report_fd,"{\"ok\":false,\"error\":\"%s\",\"original_main_called\":false}\n",reason);
    _exit(2);
}

static void crash(int signal_number)
{
    (void)signal_number;
    static const char message[]="{\"ok\":false,\"error\":\"signal_or_timeout\",\"original_main_called\":false}\n";
    ssize_t written=write(report_fd,message,sizeof(message)-1);
    (void)written;
    _exit(3);
}

static int find_main(struct dl_phdr_info *info,size_t size,void *context)
{
    (void)size;(void)context;
    if(info->dlpi_name && !info->dlpi_name[0]) {vendor_base=info->dlpi_addr;return 1;}
    return 0;
}

static void verify_binary(void)
{
    typedef unsigned char *(*sha256_fn)(const unsigned char *,size_t,unsigned char *);
    sha256_fn digest=(sha256_fn)dlsym(RTLD_DEFAULT,"SHA256");
    if(!digest) fatal("sha256_symbol_missing");
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
    struct stat st;
    if(fd<0 || fstat(fd,&st) || st.st_size!=6563736) fatal("unexpected_executable_size");
    size_t size=(size_t)st.st_size;
    void *bytes=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(bytes==MAP_FAILED) fatal("executable_map_failed");
    const Elf64_Ehdr *header=bytes;
    if(memcmp(header->e_ident,ELFMAG,SELFMAG) || header->e_machine!=EM_AARCH64 ||
       header->e_ident[EI_CLASS]!=ELFCLASS64 || header->e_ident[EI_DATA]!=ELFDATA2LSB ||
       header->e_type!=ET_DYN) fatal("wrong_executable_architecture");
    unsigned char hash[32];char hex[65];
    if(!digest(bytes,size,hash)) fatal("sha256_failed");
    for(size_t i=0;i<32;i++) snprintf(hex+i*2,3,"%02x",hash[i]);
    munmap(bytes,size);
    if(strcmp(hex,STOCK_GUI_SHA256)) fatal("unsupported_vendor_sha256");
    dl_iterate_phdr(find_main,NULL);
    if(!vendor_base || (uintptr_t)original_main_address!=vendor_base+STOCK_MAIN_OFFSET)
        fatal("original_main_address_mismatch");
    if(dlsym(RTLD_DEFAULT,"__gmon_start__")) fatal("unexpected_profiling_initializer");
}
