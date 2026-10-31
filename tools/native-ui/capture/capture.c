#define _GNU_SOURCE
#include "capture.h"
#include "../abi/stock_draw_abi.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <link.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CAPTURE_REQUEST_PATH
#define CAPTURE_REQUEST_PATH "/tmp/stock-draw-capture.request"
#endif
#ifndef CAPTURE_OUTPUT_PATH
#define CAPTURE_OUTPUT_PATH "/tmp/stock-draw-capture"
#endif

enum { WIDTH=480, HEIGHT=320, STRIDE=1920, FRAME_BYTES=STRIDE*HEIGHT,
       TASK_LIMIT=2048, DESC_LIMIT=1024, DESC_BYTES=1024*1024, GUI_BYTES=6563736 };
enum Phase { IDLE, ARMED, RECORDING, READY, FAILED, DONE };
typedef struct {
    void *target, *original;
    uint32_t code, padding;
    void *user_data, *param, *previous;
    uint8_t flags, tail[7];
} NativeEvent;
_Static_assert(sizeof(NativeEvent)==56, "verified event ABI");
typedef int32_t (*Evaluate)(stock_draw_unit_t *,stock_task_t *);
typedef struct {
    uintptr_t task, layer, parent, next_layer, draw_buffer, object;
    uint32_t type, color_format, descriptor_offset, descriptor_size;
    stock_area_t area, real_area, clip, original_clip, buffer_area;
    uint8_t preferred_unit, preference_score;
    int32_t evaluate_result;
    uint64_t elapsed_ns;
} TaskRecord;
typedef struct {
    TaskRecord tasks[TASK_LIMIT];
    uint8_t descriptors[DESC_BYTES], initial[FRAME_BYTES], final[FRAME_BYTES];
} Storage;
static struct {
    _Atomic enum Phase phase;
    const char *error;
    uintptr_t base;
    void *display;
    stock_draw_sw_unit_t *unit;
    stock_draw_buf_t *frame_buffer;
    Evaluate original;
    Storage *storage;
    unsigned task_count, descriptor_bytes, flushes;
    uint64_t next_poll, deadline, started, ended, initial_copy_ns, final_copy_ns, observer_ns;
    bool event_added, callback_restored, binary_verified;
    void (*lock)(void), (*unlock)(void);
    void *(*add_event)(void *,void (*)(NativeEvent *),uint32_t,void *);
    uint32_t (*remove_event)(void *,void (*)(NativeEvent *),void *);
} capture;

static uint64_t now_ns(void)
{
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)) return 0;
    return (uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;
}
static void fail(const char *reason)
{
    if(!capture.error) capture.error=reason;
    capture.phase=FAILED;
}
static void *pointer_at(const void *base,size_t offset)
{
    void *value; memcpy(&value,(const uint8_t *)base+offset,sizeof(value)); return value;
}
static uint32_t word_at(const void *base,size_t offset)
{
    uint32_t value; memcpy(&value,(const uint8_t *)base+offset,sizeof(value)); return value;
}
static bool valid_buffer(const stock_draw_buf_t *buffer)
{
    return buffer && buffer->header.magic==0x19 && buffer->header.cf==STOCK_CF_XRGB8888 &&
        buffer->header.width==WIDTH && buffer->header.height==HEIGHT && buffer->header.stride==STRIDE &&
        buffer->data && !((uintptr_t)buffer->data%4) && buffer->data_size==FRAME_BYTES;
}
static bool valid_display(void *display)
{
    if(!display) return false;
    stock_draw_buf_t *first=pointer_at(display,32), *second=pointer_at(display,40), *active=pointer_at(display,48);
    return word_at(display,0)==WIDTH && word_at(display,4)==HEIGHT && word_at(display,84)==1 &&
        word_at(display,92)==STOCK_CF_XRGB8888 && valid_buffer(first) && valid_buffer(second) &&
        first!=second && first->data!=second->data && (active==first || active==second) && pointer_at(display,752);
}

/* The generic descriptor prefix is verified; its remaining bytes stay opaque. */
static void record_task(stock_task_t *task,int32_t result)
{
    if(capture.phase!=RECORDING) return;
    uint64_t started=now_ns();
    if(!task || !task->draw_dsc || task->type<1 || task->type>12) { fail("unknown_task_abi"); return; }
    const stock_draw_dsc_base_t *base=task->draw_dsc;
    size_t size=base->dsc_size;
    if(!base->layer || size<sizeof(*base) || size>DESC_LIMIT || size%8 ||
       (task->type==1 && size!=112) || (task->type==4 && size!=128)) { fail("unknown_descriptor_abi"); return; }
    if(capture.task_count==TASK_LIMIT || size>DESC_BYTES-capture.descriptor_bytes) { fail("capture_capacity"); return; }
    stock_layer_t *layer=base->layer;
    TaskRecord *record=&capture.storage->tasks[capture.task_count++];
    *record=(TaskRecord){.task=(uintptr_t)task,.layer=(uintptr_t)layer,.parent=(uintptr_t)layer->parent,
        .next_layer=(uintptr_t)layer->next,.draw_buffer=(uintptr_t)layer->draw_buf,.object=(uintptr_t)base->object,
        .type=task->type,.color_format=layer->color_format,.descriptor_offset=capture.descriptor_bytes,
        .descriptor_size=(uint32_t)size,.area=task->area,.real_area=task->real_area,.clip=task->clip_area,
        .original_clip=task->clip_original,.buffer_area=layer->buf_area,.preferred_unit=task->preferred_unit,
        .preference_score=task->preference_score,.evaluate_result=result,.elapsed_ns=started-capture.started};
    memcpy(capture.storage->descriptors+capture.descriptor_bytes,task->draw_dsc,size);
    capture.descriptor_bytes+=(unsigned)size;
    capture.observer_ns+=now_ns()-started;
}
static int32_t observe_evaluate(stock_draw_unit_t *unit,stock_task_t *task)
{
    int32_t result=capture.original(unit,task);
    record_task(task,result);
    return result;
}
static void display_event(NativeEvent *event)
{
    if(event->user_data!=&capture || event->target!=capture.display) return;
    unsigned code=event->code&0x7fffu;
    if(code==38) {
        if(capture.unit->base.evaluate_cb==observe_evaluate) capture.unit->base.evaluate_cb=capture.original;
        capture.callback_restored=capture.unit->base.evaluate_cb==capture.original;
        capture.event_added=false; /* The display destructor owns its event list now. */
        fail("display_deleted");
    }
    if(capture.phase!=ARMED && capture.phase!=RECORDING) return;
    if(code==56) {
        if(capture.phase!=ARMED || !valid_display(capture.display)) { fail("render_start_abi_or_sequence"); return; }
        capture.frame_buffer=pointer_at(capture.display,48);
        capture.started=now_ns();
        memcpy(capture.storage->initial,capture.frame_buffer->data,FRAME_BYTES);
        capture.initial_copy_ns=now_ns()-capture.started;
        capture.phase=RECORDING;
    }
    else if(code==59 && capture.phase==RECORDING) {
        capture.flushes++;
        if(!word_at(capture.display,76)) return;
        if(pointer_at(capture.display,48)!=capture.frame_buffer || !valid_buffer(capture.frame_buffer)) {
            fail("final_buffer_changed"); return;
        }
        capture.ended=now_ns();
        memcpy(capture.storage->final,capture.frame_buffer->data,FRAME_BYTES);
        capture.final_copy_ns=now_ns()-capture.ended;
        capture.phase=READY;
    }
    else if(code==57 && capture.phase==RECORDING) fail("missing_final_flush");
}
static void detach_locked(void)
{
    if(capture.unit) {
        if(capture.unit->base.evaluate_cb==observe_evaluate) capture.unit->base.evaluate_cb=capture.original;
        capture.callback_restored=capture.unit->base.evaluate_cb==capture.original;
        if(!capture.callback_restored) fail("observer_callback_changed");
    }
    if(capture.event_added) {
        uint32_t removed=capture.remove_event(capture.display,display_event,&capture);
        if(removed!=1) fail("display_observer_removal_failed");
        else capture.event_added=false;
    }
}
static int main_base(struct dl_phdr_info *info,size_t size,void *data)
{
    (void)size;
    if(info->dlpi_name && !*info->dlpi_name) { *(uintptr_t *)data=info->dlpi_addr; return 1; }
    return 0;
}
static bool verify_binary(uintptr_t base)
{
    uintptr_t actual=0;
    dl_iterate_phdr(main_base,&actual);
    if(!base || actual!=base) return false;
    unsigned char *(*sha256)(const void *,size_t,unsigned char *)=dlsym(RTLD_DEFAULT,"SHA256");
    if(!sha256) return false;
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
    if(fd<0) return false;
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size!=GUI_BYTES) { close(fd); return false; }
    void *bytes=mmap(NULL,GUI_BYTES,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    if(bytes==MAP_FAILED) return false;
    unsigned char digest[32]={0};
    bool ok=sha256(bytes,GUI_BYTES,digest)!=NULL;
    munmap(bytes,GUI_BYTES);
    char hash[65];
    for(unsigned i=0;i<32;i++) snprintf(hash+2*i,3,"%02x",digest[i]);
    return ok && !strcmp(hash,STOCK_GUI_SHA256);
}
static int consume_request(const char *path)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0) return errno==ENOENT ? 0 : -1;
    struct stat st;
    char text[3]={0};
    bool regular=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_size==2;
    ssize_t count=regular?read(fd,text,sizeof(text)):-1;
    close(fd);
    if(unlink(path)) return -1;
    return count==2 && !memcmp(text,"1\n",2) ? 1 : -1;
}
static FILE *output_file(int directory,const char *name)
{
    int fd=openat(directory,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0) return NULL;
    FILE *file=fdopen(fd,"wb");
    if(!file) close(fd);
    return file;
}
static bool write_bytes(int directory,const char *name,const void *bytes,size_t size)
{
    FILE *file=output_file(directory,name);
    if(!file) return false;
    bool ok=fwrite(bytes,1,size,file)==size;
    return fclose(file)==0 && ok;
}
static void print_area(FILE *file,const stock_area_t *area)
{
    fprintf(file,"[%d,%d,%d,%d]",area->x1,area->y1,area->x2,area->y2);
}
static bool write_capture(const char *path)
{
    if(mkdir(path,0700)) return false;
    int dir=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(dir<0) return false;
    bool complete=capture.phase==READY && capture.callback_restored && !capture.event_added && capture.task_count;
    bool ok=true;
    if(capture.storage) {
        ok=write_bytes(dir,"descriptors.bin",capture.storage->descriptors,capture.descriptor_bytes);
        if(complete) ok=write_bytes(dir,"initial.bgra",capture.storage->initial,FRAME_BYTES) &&
                        write_bytes(dir,"final.bgra",capture.storage->final,FRAME_BYTES) && ok;
        FILE *tasks=output_file(dir,"tasks.jsonl");
        if(!tasks) ok=false;
        else {
            for(unsigned i=0;i<capture.task_count;i++) {
                const TaskRecord *r=&capture.storage->tasks[i];
                fprintf(tasks,"{\"sequence\":%u,\"type\":%u,\"task\":\"0x%" PRIxPTR "\",\"layer\":\"0x%" PRIxPTR
                    "\",\"parent\":\"0x%" PRIxPTR "\",\"next_layer\":\"0x%" PRIxPTR "\",\"draw_buffer\":\"0x%" PRIxPTR
                    "\",\"object\":\"0x%" PRIxPTR "\",\"color_format\":%u,\"descriptor_offset\":%u,\"dsc_size\":%u,\"area\":",
                    i,r->type,r->task,r->layer,r->parent,r->next_layer,r->draw_buffer,r->object,r->color_format,r->descriptor_offset,r->descriptor_size);
                print_area(tasks,&r->area); fputs(",\"real_area\":",tasks); print_area(tasks,&r->real_area);
                fputs(",\"clip\":",tasks); print_area(tasks,&r->clip); fputs(",\"original_clip\":",tasks); print_area(tasks,&r->original_clip);
                fputs(",\"buffer_area\":",tasks); print_area(tasks,&r->buffer_area);
                fprintf(tasks,",\"preferred_unit\":%u,\"preference_score\":%u,\"evaluate_return\":%d,\"elapsed_ns\":%" PRIu64 "}\n",
                        r->preferred_unit,r->preference_score,r->evaluate_result,r->elapsed_ns);
            }
            if(ferror(tasks)) ok=false;
            if(fclose(tasks)) ok=false;
        }
    }
    FILE *report=output_file(dir,"report.json");
    if(!report) ok=false;
    else {
        fprintf(report,"{\"complete\":%s,\"reason\":\"%s\",\"vendor_sha256\":\"%s\",\"vendor_bytes\":%u,"
            "\"binary_verified\":%s,\"callback_restored\":%s,\"event_observer_removed\":%s,"
            "\"drawing_replaced\":false,\"input_sent\":false,\"descriptor_pointers_replayable\":false,"
            "\"width\":%u,\"height\":%u,\"stride\":%u,\"color_format\":17,\"fourth_byte\":\"unused XRGB byte\","
            "\"task_count\":%u,\"descriptor_bytes\":%u,\"flushes\":%u,\"render_start_ns\":%" PRIu64
            ",\"last_flush_finish_ns\":%" PRIu64 ",\"initial_copy_ns\":%" PRIu64 ",\"final_copy_ns\":%" PRIu64
            ",\"task_observer_ns\":%" PRIu64 "}\n",
            complete&&ok?"true":"false",capture.error?capture.error:(ok?(complete?"ok":"no_tasks"):"output_failed"),
            STOCK_GUI_SHA256,GUI_BYTES,capture.binary_verified?"true":"false",capture.callback_restored?"true":"false",
            !capture.event_added?"true":"false",WIDTH,HEIGHT,STRIDE,capture.task_count,capture.descriptor_bytes,capture.flushes,
            capture.started,capture.ended,capture.initial_copy_ns,capture.final_copy_ns,capture.observer_ns);
        if(ferror(report)) ok=false;
        if(fclose(report)) ok=false;
    }
    close(dir);
    return ok;
}
void stock_draw_tick(uintptr_t base,void *display)
{
    if(capture.phase==DONE) return;
    uint64_t now=now_ns();
    if(capture.phase==IDLE) {
        if(now<capture.next_poll) return;
        capture.next_poll=now+UINT64_C(1000000000);
        int request=consume_request(CAPTURE_REQUEST_PATH);
        if(!request) return;
        if(request<0) fail("invalid_request");
        else if(!verify_binary(base)) fail("unsupported_binary");
        else {
            capture.binary_verified=true; capture.base=base; capture.display=display;
            capture.lock=(void (*)(void))(base+0x3fcf0u); capture.unlock=(void (*)(void))(base+0x3fd10u);
            capture.add_event=(void *(*)(void *,void (*)(NativeEvent *),uint32_t,void *))(base+0xcb7b0u);
            capture.remove_event=(uint32_t (*)(void *,void (*)(NativeEvent *),void *))(base+0xcb8f0u);
            capture.storage=calloc(1,sizeof(Storage));
            if(!capture.storage) fail("allocation_failed");
            else {
                capture.lock();
                void *global=*(void **)(base+STOCK_GLOBAL_GOT_OFFSET);
                stock_draw_sw_unit_t *unit=global?pointer_at(global,STOCK_UNIT_HEAD_OFFSET):NULL;
                if(!global || !display || pointer_at(global,0x28)!=display || !valid_display(display) || !unit ||
                   word_at(global,STOCK_UNIT_COUNT_OFFSET)!=1 || unit->base.next || unit->task_active ||
                   !unit->initialized || (uintptr_t)unit->base.evaluate_cb!=base+STOCK_SW_EVALUATE_OFFSET ||
                   (uintptr_t)unit->base.dispatch_cb!=base+STOCK_SW_DISPATCH_OFFSET ||
                   (uintptr_t)unit->base.delete_cb!=base+STOCK_SW_DELETE_OFFSET) fail("unsupported_live_abi");
                else {
                    capture.unit=unit; capture.original=unit->base.evaluate_cb;
                    if(!capture.add_event(display,display_event,0,&capture)) fail("display_observer_registration_failed");
                    else {
                        capture.event_added=true; unit->base.evaluate_cb=observe_evaluate;
                        capture.deadline=now_ns()+UINT64_C(3000000000); capture.phase=ARMED;
                    }
                }
                capture.unlock();
            }
        }
    }
    if(capture.phase==ARMED || capture.phase==RECORDING || capture.phase==READY || capture.phase==FAILED) {
        if(capture.unit) {
            capture.lock();
            if((capture.phase==ARMED || capture.phase==RECORDING) && now>=capture.deadline) fail("capture_timeout");
            if(capture.phase==READY || capture.phase==FAILED) detach_locked();
            capture.unlock();
        }
        if(capture.phase==READY || capture.phase==FAILED) {
            if(!write_capture(CAPTURE_OUTPUT_PATH)) dprintf(STDERR_FILENO,"stock_draw_capture: output failed\n");
            if(!capture.event_added && (!capture.unit || capture.callback_restored)) { free(capture.storage); capture.storage=NULL; }
            capture.phase=DONE;
        }
    }
}
