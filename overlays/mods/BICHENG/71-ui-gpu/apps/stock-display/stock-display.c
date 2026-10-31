#define _GNU_SOURCE
#include "stock-perf.h"
#include <dlfcn.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    const char *version, *sha256;
    size_t executable_size;
    uintptr_t global_got, lock, unlock, timer_set_period, animation_get_timer, invalidate_area;
    size_t global_display_offset, display_timer_offset;
    uintptr_t main_sleep, main_short_sleep;
} DisplayAbi;

/* The shared offsets match the hash-verified StockAbi records used by the G-code extension. */
static const DisplayAbi display_abis[] = {
    {.version="2.0.0.205",.sha256="bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4",
     .executable_size=7010568,.global_got=0x69ff40,.lock=0x6cfb0,.unlock=0x6cfd0,.timer_set_period=0x7a130,
     .global_display_offset=0x28,.display_timer_offset=0x380,.main_sleep=0x3bb78,.main_short_sleep=0x3bb90},
    {.version="2.1.0.157",.sha256="2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472",
     .executable_size=6563736,.global_got=0x640f38,.lock=0x3fcf0,.unlock=0x3fd10,.timer_set_period=0x4ce70,
     .animation_get_timer=0x44350,.invalidate_area=0x55c00,.global_display_offset=0x28,.display_timer_offset=0x380,
     .main_sleep=0xe8b8,.main_short_sleep=0xe8d0}
};

static uintptr_t vendor_base;
static const DisplayAbi *vendor_abi;
static int (*original_usleep)(useconds_t);
static unsigned (*extension_tick)(unsigned);
static int main_initialized, gpu_enabled, policy_reported, unsupported_reported;
static uint64_t benchmark_until, benchmark_next, benchmark_checked;

static void display_log(const char *format, ...)
{
    FILE *file=fopen("/tmp/stock-display.log","a");
    if(!file) return;
    va_list args; va_start(args,format); vfprintf(file,format,args); va_end(args);
    fclose(file);
}

static uint64_t display_milliseconds(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now)) return 0;
    return (uint64_t)now.tv_sec*1000u+(uint64_t)now.tv_nsec/1000000u;
}

static void benchmark_poll(uint64_t now)
{
    if(!now) return;
    if(benchmark_until && now>=benchmark_until) {
        display_log("repaint benchmark ended at monotonic_ms=%llu\n",(unsigned long long)now);
        benchmark_until=benchmark_next=0;
    }
    if(benchmark_checked && now-benchmark_checked<1000) return;
    benchmark_checked=now;
    const char *path="/tmp/stock-display-benchmark-seconds";
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return;
    char text[24]={0};
    ssize_t length=read(fd,text,sizeof(text)-1);
    close(fd);
    if(unlink(path)) return;
    char *end=NULL;
    long seconds=length>0?strtol(text,&end,10):0;
    if(end) while(isspace((unsigned char)*end)) end++;
    if(length<=0 || length==(ssize_t)sizeof(text)-1 || end==text || !end || *end || seconds<1 || seconds>15) {
        display_log("repaint benchmark rejected: duration must be 1..15 seconds\n");
        return;
    }
    if(benchmark_until || strcmp(vendor_abi->version,"2.1.0.157")) {
        display_log("repaint benchmark rejected: already active or unsupported GUI version\n");
        return;
    }
    benchmark_until=now+(uint64_t)seconds*1000u;
    benchmark_next=now;
    display_log("repaint benchmark started at monotonic_ms=%llu duration_seconds=%ld; input and UI objects unchanged\n",
                (unsigned long long)now,seconds);
}

static const DisplayAbi *matching_abi(size_t size, const char *hash)
{
    for(size_t i=0;i<sizeof(display_abis)/sizeof(display_abis[0]);i++)
        if(size==display_abis[i].executable_size && !strcmp(hash,display_abis[i].sha256)) return &display_abis[i];
    return NULL;
}

static int locate_vendor(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    if(info->dlpi_name && !info->dlpi_name[0]) {
        *(uintptr_t *)data=info->dlpi_addr;
        return 1;
    }
    return 0;
}

__attribute__((constructor)) static void display_init(void)
{
    original_usleep=dlsym(RTLD_NEXT,"usleep");
    char path[128]={0};
    ssize_t length=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(length<0 || (size_t)length>=sizeof(path)-1 || strcmp(path,"/usr/bin/gui")) return;
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
    if(fd<0) return;
    struct stat info;
    if(fstat(fd,&info)) { close(fd); return; }
    int supported_size=0;
    for(size_t i=0;i<sizeof(display_abis)/sizeof(display_abis[0]);i++)
        if(info.st_size==(off_t)display_abis[i].executable_size) supported_size=1;
    if(!supported_size) { close(fd); return; }
    unsigned char *(*sha256)(const void *,size_t,unsigned char *)=dlsym(RTLD_DEFAULT,"SHA256");
    if(!sha256) { close(fd); return; }
    void *bytes=mmap(NULL,(size_t)info.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    if(bytes==MAP_FAILED) return;
    unsigned char digest[32];
    int valid=sha256(bytes,(size_t)info.st_size,digest)!=NULL;
    munmap(bytes,(size_t)info.st_size);
    if(!valid) return;
    char hash[65];
    for(unsigned i=0;i<32;i++) snprintf(hash+i*2,3,"%02x",digest[i]);
    const DisplayAbi *abi=matching_abi((size_t)info.st_size,hash);
    if(!abi) return;
    uintptr_t base=0;
    dl_iterate_phdr(locate_vendor,&base);
    if(base) { vendor_base=base; vendor_abi=abi; }
}

static unsigned native_tick(unsigned sleep_us, uintptr_t caller)
{
    const DisplayAbi *abi=vendor_abi;
    if(!abi || !vendor_base || (caller-vendor_base!=abi->main_sleep && caller-vendor_base!=abi->main_short_sleep))
        return sleep_us;
    if(!main_initialized) {
        const char *enabled=getenv("UI_GPU_ENABLED");
        gpu_enabled=enabled && !strcmp(enabled,"1");
        extension_tick=dlsym(RTLD_DEFAULT,"stock_ui_tick");
        main_initialized=1;
    }
    uint64_t now=display_milliseconds();
    benchmark_poll(now);
    ((void (*)(void))(vendor_base+abi->lock))();
    void *global=*(void **)(vendor_base+abi->global_got);
    void *display=global?*(void **)((char *)global+abi->global_display_offset):NULL;
    void *refresh=display?*(void **)((char *)display+abi->display_timer_offset):NULL;
    unsigned before=0, refresh_period=0, animation_before=0, animation_period=0;
    int perf_result=-1, benchmark_cancelled=0;
    if(refresh) {
        void (*set_period)(void *,unsigned)=(void (*)(void *,unsigned))(vendor_base+abi->timer_set_period);
        before=*(unsigned *)refresh;
        if(gpu_enabled && before!=22) set_period(refresh,22);
        refresh_period=*(unsigned *)refresh;
        if(abi->animation_get_timer) {
            void *animation=((void *(*)(void))(vendor_base+abi->animation_get_timer))();
            if(animation) {
                animation_before=*(unsigned *)animation;
                if(gpu_enabled && animation_before!=22) set_period(animation,22);
                animation_period=*(unsigned *)animation;
            }
        }
        perf_result=stock_perf_attach(vendor_base,abi->version,display,gpu_enabled,refresh_period,animation_period);
        if(benchmark_until && now<benchmark_until && now>=benchmark_next && refresh_period) {
            const int32_t *resolution=display;
            if(resolution[0]==480 && resolution[1]==320) {
                const int32_t area[]={0,0,479,319};
                ((void (*)(void *,const int32_t *))(vendor_base+abi->invalidate_area))(display,area);
                benchmark_next=now+refresh_period;
            }
            else {
                benchmark_until=benchmark_next=0;
                benchmark_cancelled=1;
            }
        }
    }
    ((void (*)(void))(vendor_base+abi->unlock))();
    stock_perf_poll();
    if(benchmark_cancelled) display_log("repaint benchmark cancelled: expected the verified 480x320 display\n");
    if(refresh && !policy_reported) {
        display_log("GUI %s: gpu_output_enabled=%d refresh_ms=%u->%u animation_ms=%u->%u; stock controls remain CPU-rendered\n",
                    abi->version,gpu_enabled,before,refresh_period,animation_before,animation_period);
        policy_reported=1;
    }
    if(perf_result==0 && !unsupported_reported) {
        display_log("GUI %s: display performance events are unsupported; animation timer unchanged when its ABI is unknown\n",abi->version);
        unsupported_reported=1;
    }
    return refresh && extension_tick?extension_tick(sleep_us):sleep_us;
}

int usleep(useconds_t usec)
{
    if(!original_usleep) original_usleep=dlsym(RTLD_NEXT,"usleep");
    if(!original_usleep) { errno=ENOSYS; return -1; }
    unsigned delay=native_tick(usec,(uintptr_t)__builtin_return_address(0));
    return original_usleep(delay);
}
