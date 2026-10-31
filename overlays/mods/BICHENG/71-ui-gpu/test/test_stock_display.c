#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void *test_dlsym(void *,const char *);
static ssize_t test_readlink(const char *,char *,size_t);
static int test_open(const char *,int,...);
static int test_unlink(const char *);
static int test_phdr(int (*)(struct dl_phdr_info *,size_t,void *),void *);
static FILE *test_fopen(const char *,const char *);
static int test_clock(clockid_t,struct timespec *);
#define dlsym test_dlsym
#define readlink test_readlink
#define open test_open
#define unlink test_unlink
#define dl_iterate_phdr test_phdr
#define fopen test_fopen
#define clock_gettime test_clock
#define usleep intercepted_usleep
#include "../apps/stock-display/stock-display.c"
#undef dlsym
#undef readlink
#undef open
#undef unlink
#undef dl_iterate_phdr
#undef fopen
#undef clock_gettime
#undef usleep

static char fixture_path[256], request_path[256];
static int pretend_gui, hash_kind=-1, hash_available=1, have_extension, mutex_depth;
static unsigned locks, unlocks, timer_writes, attachments, polls, ticks, tick_lookups, logs, repaints;
static unsigned actual_refresh, actual_animation, attached_gpu, slept;
static uint64_t fake_ms=100000;
static unsigned refresh_timer=33, animation_timer=33;
static _Alignas(8) unsigned char global_memory[64], display_memory[928];
static void *global_pointer=global_memory;
static DisplayAbi fake_abi;

static int sleep_original(useconds_t value) { slept=value; return 7; }
static unsigned tick_extension(unsigned value) { assert(!mutex_depth); ticks++; return value<17000?value:17000; }
static void lock_native(void) { assert(!mutex_depth); mutex_depth=1; locks++; }
static void unlock_native(void) { assert(mutex_depth==1); mutex_depth=0; unlocks++; }
static void set_period(void *timer,unsigned period) { assert(mutex_depth==1); *(unsigned *)timer=period; timer_writes++; }
static void *get_animation(void) { assert(mutex_depth==1); return &animation_timer; }
static void invalidate_native(void *display,const int32_t *area)
{
    assert(mutex_depth==1 && display==display_memory);
    assert(area[0]==0 && area[1]==0 && area[2]==479 && area[3]==319); repaints++;
}

int stock_perf_attach(uintptr_t base,const char *version,void *display,int gpu,unsigned refresh,unsigned animation)
{
    assert(base==vendor_base && display==display_memory && mutex_depth==1);
    attachments++; actual_refresh=refresh; actual_animation=animation; attached_gpu=gpu;
    return strcmp(version,"2.1.0.157")?0:1;
}
void stock_perf_poll(void) { assert(!mutex_depth); polls++; }

static unsigned char *fake_sha(const void *bytes,size_t size,unsigned char *digest)
{
    assert(bytes && size>0);
    memset(digest,0,32);
    if(hash_kind>=0) for(unsigned i=0;i<32;i++) {
        unsigned value; assert(sscanf(display_abis[hash_kind].sha256+i*2,"%2x",&value)==1); digest[i]=value;
    }
    return digest;
}
static void *test_dlsym(void *handle,const char *name)
{
    if(!strcmp(name,"usleep")) { assert(handle==RTLD_NEXT); return sleep_original; }
    assert(handle==RTLD_DEFAULT);
    if(!strcmp(name,"SHA256")) return hash_available?fake_sha:NULL;
    assert(!strcmp(name,"stock_ui_tick")); tick_lookups++;
    return have_extension?tick_extension:NULL;
}
static ssize_t test_readlink(const char *path,char *buffer,size_t size)
{
    assert(!strcmp(path,"/proc/self/exe"));
    if(!pretend_gui) return readlink(path,buffer,size);
    const char *name="/usr/bin/gui"; assert(size>strlen(name)); memcpy(buffer,name,strlen(name)); return strlen(name);
}
static int test_open(const char *path,int flags,...)
{
    if(!strcmp(path,"/proc/self/exe")) return open(fixture_path,flags);
    assert(!strcmp(path,"/tmp/stock-display-benchmark-seconds")); return open(request_path,flags);
}
static int test_unlink(const char *path)
{
    assert(!strcmp(path,"/tmp/stock-display-benchmark-seconds")); return unlink(request_path);
}
static int test_phdr(int (*callback)(struct dl_phdr_info *,size_t,void *),void *data)
{
    struct dl_phdr_info info={.dlpi_name="",.dlpi_addr=0x100000};
    return callback(&info,sizeof(info),data);
}
static FILE *test_fopen(const char *path,const char *mode)
{
    assert(!strcmp(path,"/tmp/stock-display.log") && !strcmp(mode,"a")); logs++; return tmpfile();
}
static int test_clock(clockid_t clock,struct timespec *value)
{
    assert(clock==CLOCK_MONOTONIC); value->tv_sec=fake_ms/1000; value->tv_nsec=(fake_ms%1000)*1000000; return 0;
}

static void reset_policy(unsigned version,int enabled,int extension)
{
    main_initialized=policy_reported=unsupported_reported=0; extension_tick=NULL;
    benchmark_until=benchmark_next=benchmark_checked=0;
    locks=unlocks=timer_writes=attachments=polls=ticks=tick_lookups=logs=repaints=0;
    refresh_timer=animation_timer=33; have_extension=extension;
    global_pointer=global_memory;
    memset(global_memory,0,sizeof(global_memory)); memset(display_memory,0,sizeof(display_memory));
    *(void **)(global_memory+0x28)=display_memory;
    *(void **)(display_memory+0x380)=&refresh_timer;
    ((int32_t *)display_memory)[0]=480; ((int32_t *)display_memory)[1]=320;
    vendor_base=(uintptr_t)&global_pointer;
    fake_abi=display_abis[version]; fake_abi.global_got=0;
    fake_abi.lock=(uintptr_t)lock_native-vendor_base; fake_abi.unlock=(uintptr_t)unlock_native-vendor_base;
    fake_abi.timer_set_period=(uintptr_t)set_period-vendor_base;
    if(fake_abi.animation_get_timer) fake_abi.animation_get_timer=(uintptr_t)get_animation-vendor_base;
    if(fake_abi.invalidate_area) fake_abi.invalidate_area=(uintptr_t)invalidate_native-vendor_base;
    vendor_abi=&fake_abi;
    assert(!setenv("UI_GPU_ENABLED",enabled?"1":"0",1));
}

static void request(const char *seconds)
{
    FILE *file=fopen(request_path,"w"); assert(file); assert(fputs(seconds,file)>=0); assert(!fclose(file));
    fake_ms+=1000;
}

int main(void)
{
    char directory[]="/tmp/stock-display-test.XXXXXX"; assert(mkdtemp(directory));
    snprintf(fixture_path,sizeof(fixture_path),"%s/gui",directory);
    snprintf(request_path,sizeof(request_path),"%s/request",directory);
    int fd=open(fixture_path,O_RDWR|O_CREAT|O_TRUNC,0600); assert(fd>=0);
    pretend_gui=1;
    for(int i=0;i<2;i++) {
        assert(!ftruncate(fd,display_abis[i].executable_size)); hash_kind=i;
        vendor_abi=NULL; vendor_base=0; display_init();
        assert(vendor_abi==&display_abis[i] && vendor_base==0x100000);
        assert(!locks && !tick_lookups && !attachments);
    }
    hash_kind=-1; vendor_abi=NULL; vendor_base=0; display_init(); assert(!vendor_abi && !vendor_base);
    hash_kind=1; hash_available=0; display_init(); assert(!vendor_abi); hash_available=1;
    assert(!ftruncate(fd,1)); display_init(); assert(!vendor_abi); close(fd);

    reset_policy(1,0,0);
    assert(native_tick(99000,vendor_base+fake_abi.main_sleep+4)==99000 && !locks && !tick_lookups);
    assert(native_tick(99000,vendor_base+fake_abi.main_sleep)==99000);
    assert(locks==1 && unlocks==1 && !timer_writes && actual_refresh==33 && actual_animation==33 && !attached_gpu);
    refresh_timer=17;
    native_tick(500,vendor_base+fake_abi.main_short_sleep); assert(refresh_timer==17 && actual_refresh==17 && !timer_writes);
    assert(intercepted_usleep(123)==7 && slept==123);

    reset_policy(1,1,1);
    assert(native_tick(99000,vendor_base+fake_abi.main_sleep)==17000);
    assert(timer_writes==2 && actual_refresh==22 && actual_animation==22 && attached_gpu && ticks==1);
    native_tick(900,vendor_base+fake_abi.main_short_sleep);
    assert(timer_writes==2 && tick_lookups==1 && ticks==2 && polls==2);
    unsigned log_count=logs; native_tick(900,vendor_base+fake_abi.main_sleep); assert(logs==log_count);

    reset_policy(0,1,1);
    native_tick(99000,vendor_base+fake_abi.main_sleep);
    assert(timer_writes==1 && refresh_timer==22 && animation_timer==33 && actual_animation==0 && ticks==1);
    assert(logs==2); native_tick(99000,vendor_base+fake_abi.main_short_sleep); assert(logs==2);
    request("15\n"); native_tick(99000,vendor_base+fake_abi.main_sleep); assert(!benchmark_until && !repaints);

    reset_policy(1,0,0);
    global_pointer=NULL;
    assert(!mkfifo(request_path,0600));
    alarm(2);
    native_tick(1000,vendor_base+fake_abi.main_sleep);
    alarm(0);
    assert(!benchmark_until && !repaints && access(request_path,F_OK)!=0);
    assert(!attachments && !ticks && !timer_writes);
    global_pointer=global_memory;
    request("16\n"); native_tick(1000,vendor_base+fake_abi.main_sleep); assert(!benchmark_until && !repaints);
    request("2\n"); native_tick(1000,vendor_base+fake_abi.main_sleep);
    assert(benchmark_until==fake_ms+2000 && repaints==1 && access(request_path,F_OK)!=0 && !timer_writes);
    fake_ms+=32; native_tick(1000,vendor_base+fake_abi.main_sleep); assert(repaints==1);
    fake_ms++; native_tick(1000,vendor_base+fake_abi.main_sleep); assert(repaints==2);
    fake_ms+=2000; native_tick(1000,vendor_base+fake_abi.main_sleep); assert(!benchmark_until && repaints==2);
    request("1junk\n"); native_tick(1000,vendor_base+fake_abi.main_sleep); assert(!benchmark_until && repaints==2);
    ((int32_t *)display_memory)[0]=100;
    request("1\n"); native_tick(1000,vendor_base+fake_abi.main_sleep); assert(!benchmark_until && repaints==2);
    assert(!unlink(fixture_path)); assert(!rmdir(directory));
    puts("stock-display host tests passed: hash gate, callsite, off/on, animation, optional tick, unsupported perf, bounded repaint");
    return 0;
}
