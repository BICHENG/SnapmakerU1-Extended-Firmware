#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t fake_time = UINT64_C(1000000000);
static char output_path[256];
static unsigned replacements, registrations;
static int fail_rename;

static int test_clock_gettime(clockid_t id, struct timespec *now)
{
    assert(id==CLOCK_MONOTONIC);
    now->tv_sec=fake_time/UINT64_C(1000000000);
    now->tv_nsec=fake_time%UINT64_C(1000000000);
    return 0;
}

static int test_rename(const char *source_path, const char *target_path)
{
    assert(!strcmp(target_path,"/tmp/stock-ui-perf.json"));
    assert(!strncmp(source_path,"/tmp/.stock-ui-perf.",20));
    if(fail_rename) { fail_rename=0; errno=EIO; return -1; }
    int result=rename(source_path,output_path);
    if(!result) { replacements++; assert(access(source_path,F_OK)!=0); }
    return result;
}

#define clock_gettime test_clock_gettime
#define rename test_rename
#include "../apps/stock-display/stock-perf.c"
#undef clock_gettime
#undef rename

static void (*registered_callback)(NativeEvent *);
static void *registered_display;

static void register_callback(void *display, void (*callback)(NativeEvent *), uint32_t filter, void *data)
{
    assert(filter==0 && data==NULL);
    registrations++; registered_callback=callback; registered_display=display;
}

static void emit(unsigned code, uint64_t time, const int32_t *area)
{
    assert(time>=fake_time); fake_time=time;
    NativeEvent event={.target=registered_display,.original=registered_display,.code=code,.param=(void *)area};
    registered_callback(&event);
}

static void read_output(char *text, size_t capacity)
{
    FILE *file=fopen(output_path,"r"); assert(file);
    size_t size=fread(text,1,capacity-1,file);
    assert(feof(file)); text[size]=0; assert(!fclose(file));
}

int main(void)
{
    char directory[]="/tmp/stock-perf-test.XXXXXX";
    assert(mkdtemp(directory));
    assert(snprintf(output_path,sizeof(output_path),"%s/result.json",directory)>0);
    uintptr_t base=(uintptr_t)register_callback-0xcb7b0u;
    void *display=(void *)(uintptr_t)0x1234;
    assert(stock_perf_attach(base,"2.0.0.205",display,0,33,0)==0);
    stock_perf_poll(); assert(!registrations && access(output_path,F_OK)!=0);
    assert(stock_perf_attach(base,"2.1.0.157",display,0,33,33)==1);
    assert(stock_perf_attach(base,"2.1.0.157",display,0,33,33)==1 && registrations==1);
    emit(REFR_START,UINT64_C(2000000000),NULL);
    emit(REFR_READY,UINT64_C(2000500000),NULL);
    stock_perf_poll();
    fake_time=UINT64_C(6000000000); stock_perf_poll();
    char text[4096], old_text[4096]; read_output(text,sizeof(text));
    assert(strstr(text,"\"mode\":\"gpu_off\"") && strstr(text,"\"refresh_period_ms\":33"));
    assert(strstr(text,"\"animation_period_ms\":33") && strstr(text,"\"gpu_output_enabled\":false"));
    assert(strstr(text,"\"rendered_frames\":0") && strstr(text,"\"actual_fps\":0.0000"));
    assert(strstr(text,"\"idle_refreshes\":1") && strstr(text,"\"drawn_frame_refresh_ms\":null"));

    assert(stock_perf_attach(base,"2.1.0.157",display,1,22,22)==1 && registrations==1);
    stock_perf_poll();
    const int32_t large_area[]={10,20,19,39}, small_area[]={-2,8,0,11};
    for(unsigned i=0;i<20;i++) {
        uint64_t start=UINT64_C(6100000000)+(uint64_t)i*100000000;
        uint64_t duration=(i==19?30:i+1)*UINT64_C(1000000);
        emit(REFR_START,start,NULL);
        emit(WAIT_START,start+20000,NULL); emit(WAIT_FINISH,start+50000,NULL);
        emit(RENDER_START,start+100000,NULL);
        if(i==19) {
            emit(FLUSH_START,start+duration-600000,small_area);
            emit(FLUSH_FINISH,start+duration-500000,small_area);
        }
        emit(FLUSH_START,start+duration-300000,large_area);
        emit(FLUSH_FINISH,start+duration-200000,large_area);
        emit(RENDER_READY,start+duration-100000,NULL);
        emit(REFR_READY,start+duration,NULL);
        stock_perf_poll();
    }
    fake_time=UINT64_C(11000000000); stock_perf_poll(); read_output(text,sizeof(text));
    assert(strstr(text,"\"complete\":true") && strstr(text,"\"mode\":\"gpu_on\""));
    assert(strstr(text,"\"refresh_period_ms\":22") && strstr(text,"\"rendered_frames\":20"));
    assert(strstr(text,"\"animation_period_ms\":22") && strstr(text,"\"gpu_output_enabled\":true"));
    assert(strstr(text,"\"actual_fps\":4.0000") && strstr(text,"\"submitted_pixels\":4012"));
    assert(strstr(text,"\"flush_calls\":21") && strstr(text,"\"flush_callback_total_ms\":2.1000"));
    assert(strstr(text,"\"flush_wait_total_ms\":0.6000") && strstr(text,"\"over_budget_frames\":1"));
    assert(strstr(text,"\"over_budget_percent\":5.000"));
    assert(strstr(text,"\"drawn_frame_refresh_ms\":{\"p50\":10.0000,\"p95\":19.0000}"));
    fake_time=UINT64_C(13500000000); stock_perf_poll();
    assert(window.count==0 && replacements==2);
    fake_time=UINT64_C(16000000000); stock_perf_poll(); read_output(text,sizeof(text));
    assert(strstr(text,"\"rendered_frames\":0") && strstr(text,"\"submitted_pixels\":0"));

    emit(REFR_READY,UINT64_C(16100000000),NULL);
    fake_time=UINT64_C(21000000000); stock_perf_poll(); read_output(text,sizeof(text));
    assert(strstr(text,"\"complete\":false") && strstr(text,"\"sequence_errors\":1"));
    assert(stock_perf_attach(base,"2.1.0.157",display,0,33,33)==1);
    for(unsigned i=0;i<EVENT_CAPACITY+1;i++) emit(REFR_START,fake_time+100,NULL);
    fake_time=UINT64_C(26000000000); stock_perf_poll(); read_output(text,sizeof(text));
    assert(strstr(text,"\"dropped_events\":1") && strstr(text,"\"complete\":false"));
    emit(EVENT_DELETE,UINT64_C(26100000000),NULL);
    fake_time=UINT64_C(31100000000); stock_perf_poll(); read_output(text,sizeof(text));
    assert(strstr(text,"\"attached\":false"));
    strcpy(old_text,text);
    fail_rename=1;
    fake_time=UINT64_C(36100000000); stock_perf_poll(); read_output(text,sizeof(text));
    assert(!strcmp(old_text,text));
    fake_time=UINT64_C(41100000000); stock_perf_poll(); read_output(text,sizeof(text));
    assert(strstr(text,"\"write_failures\":1"));
    assert(!unlink(output_path)); assert(!rmdir(directory));
    puts("stock-perf host tests passed: idle, frames, areas, percentiles, rolling window, mode, overflow, atomic output");
    return 0;
}
