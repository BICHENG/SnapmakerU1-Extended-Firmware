// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_dispatch.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef STOCK_GPU_LAB
#include <execinfo.h>
#endif

enum { WIDTH=480, HEIGHT=320, BYTES=480*320*4 };
typedef struct {
    void *target,*original;uint32_t code,padding;
    void *user_data,*param,*previous;uint8_t flags,tail[7];
} NativeEvent;
_Static_assert(sizeof(NativeEvent)==56,"native event ABI");
static struct {
    uintptr_t base;
    void *display;
    UiGpu2D *gpu;
    atomic_uint asset_tasks;
    NativeDispatch adapter;
    void (*flush)(void *,const stock_area_t *,uint8_t *);
    void (*lock)(void),(*unlock)(void);
    uint32_t (*remove_event)(void *,void (*)(NativeEvent *),void *);
    bool attempted,attached,started,recover;
    unsigned render_mode;
    pthread_t owner;
    const char *reason;
    unsigned frames,rejected;
    uint64_t last_report;
#ifdef STOCK_GPU_LAB
    bool inject_failure;
#endif
} renderer;

static void *pointer(const void *p,size_t offset)
{
    void *value;memcpy(&value,(const uint8_t *)p+offset,sizeof(value));return value;
}
static uint32_t word(const void *p,size_t offset)
{
    uint32_t value;memcpy(&value,(const uint8_t *)p+offset,sizeof(value));return value;
}
static bool valid_buffer(const stock_draw_buf_t *buffer)
{
    return buffer&&buffer->data&&buffer->header.magic==0x19&&
        buffer->header.cf==STOCK_CF_XRGB8888&&buffer->header.width==WIDTH&&
        buffer->header.height==HEIGHT&&buffer->header.stride==WIDTH*4&&buffer->data_size==BYTES;
}
static void report(bool force)
{
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    uint64_t now=(uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
    if(!force&&renderer.last_report&&now-renderer.last_report<5000)return;
    renderer.last_report=now;
    char path[]="/tmp/ui-renderer.XXXXXX";
    int fd=mkstemp(path);if(fd<0)return;
    FILE *file=fdopen(fd,"w");
    if(!file){close(fd);unlink(path);return;}
    fprintf(file,"{\"pid\":%ld,\"ui\":\"stock\",\"renderer\":\"%s\",\"frames\":%u,"
        "\"gpu_tasks\":%u,\"cpu_asset_tasks\":%u,\"cpu_tasks\":%u,\"rejected_frames\":%u,\"reason\":\"%s\"}\n",
        (long)getpid(),renderer.attached&&!renderer.recover?"nanovg-gles3":"software-recovery",
        renderer.frames,renderer.adapter.gpu_tasks,atomic_load(&renderer.asset_tasks),renderer.adapter.cpu_tasks,renderer.rejected,
        renderer.reason?renderer.reason:"");
    if(fclose(file)==0)rename(path,"/tmp/ui-renderer.json");
    unlink(path);
}
static void fail(const char *reason)
{
    if(!renderer.reason)renderer.reason=reason;
    renderer.recover=true;renderer.adapter.enabled=false;
}
static int32_t dispatch(stock_draw_unit_t *unit,stock_layer_t *layer)
{
    /* Standalone canvases prepare assets on factory worker threads. Their native
     * CPU contract is separate from the display tree and its EGL context. */
    const stock_layer_t *root=layer;
    while(root&&root->parent)root=root->parent;
    if(root!=renderer.adapter.root) {
        int32_t result=renderer.adapter.original(unit,layer);
        if(result==1)atomic_fetch_add(&renderer.asset_tasks,1);
        return result;
    }
    if(!pthread_equal(pthread_self(),renderer.owner)) {
#ifdef STOCK_GPU_LAB
        const char *dir=getenv("NATIVE_GPU_PROBE_DIR");char path[512];
        if(dir&&!renderer.recover) {
            snprintf(path,sizeof(path),"%s/thread.jsonl",dir);FILE *file=fopen(path,"a");
            if(file) {
                fprintf(file,"{\"started\":%s,\"root\":%s,\"format\":%u,\"area\":[%d,%d,%d,%d],\"stack\":[",
                    renderer.started?"true":"false",layer==renderer.adapter.root?"true":"false",layer->color_format,
                    layer->buf_area.x1,layer->buf_area.y1,layer->buf_area.x2,layer->buf_area.y2);
                void *trace[20];int n=backtrace(trace,20);
                for(int i=0;i<n;++i)fprintf(file,"%s\"0x%lx\"",i?",":"",(unsigned long)((uintptr_t)trace[i]-renderer.base));
                fprintf(file,"]}\n");fclose(file);
            }
        }
#endif
        fail("drawing-thread-changed");
    }
#ifdef STOCK_GPU_LAB
    if(renderer.inject_failure&&renderer.started){renderer.inject_failure=false;renderer.adapter.paint.error="injected-task";}
#endif
    unsigned before=renderer.adapter.cpu_tasks;
    int32_t result=native_gpu_dispatch(&renderer.adapter,unit,layer);
#ifdef STOCK_GPU_LAB
    if(renderer.adapter.cpu_tasks>before&&!renderer.recover) {
        const char *dir=getenv("NATIVE_GPU_PROBE_DIR");char path[512];
        if(dir) {
            snprintf(path,sizeof(path),"%s/cpu-dispatch.jsonl",dir);
            FILE *file=fopen(path,"a");
            if(file) {
                fprintf(file,"{\"enabled\":%s,\"started\":%s,\"root\":%s,\"frame\":%u,\"area\":[%d,%d,%d,%d],\"stack\":[",
                    renderer.adapter.enabled?"true":"false",renderer.started?"true":"false",layer==renderer.adapter.root?"true":"false",
                    renderer.frames,layer->buf_area.x1,layer->buf_area.y1,layer->buf_area.x2,layer->buf_area.y2);
                void *trace[16];int n=backtrace(trace,16);
                for(int i=0;i<n;++i)fprintf(file,"%s\"0x%lx\"",i?",":"",(unsigned long)((uintptr_t)trace[i]-renderer.base));
                fprintf(file,"]}\n");fclose(file);
            }
        }
    }
#else
    (void)before;
#endif
    return result;
}
static void flush(void *display,const stock_area_t *area,uint8_t *pixels)
{
    stock_draw_buf_t *buffer=pointer(display,48);
    if(!renderer.started||display!=renderer.display||!area||area->x1<0||area->y1<0||
       area->x1>area->x2||area->y1>area->y2||area->x2>=WIDTH||area->y2>=HEIGHT||
       !valid_buffer(buffer)||pixels!=buffer->data)fail("display-layout-changed");
    if(!word(display,76)) {
        ((void (*)(void *))(renderer.base+0xcae40u))(display);
        return;
    }
    renderer.adapter.enabled=false;
    const uint8_t *source=renderer.started?ui_gpu2d_finish(renderer.gpu):NULL;
    renderer.started=false;
    if(!source)fail("gpu-output");
    if(renderer.adapter.paint.error)fail(renderer.adapter.paint.error);
    if(source) {
        if(!renderer.recover)memcpy(pixels,source,BYTES);
        if(!ui_gpu2d_read_end(renderer.gpu))fail("gpu-readback");
    }
    if(renderer.recover) {
        ++renderer.rejected;
        ((void (*)(void *))(renderer.base+0xcae40u))(display);
        report(true);return;
    }
    stock_area_t full={0,0,WIDTH-1,HEIGHT-1};
    ++renderer.frames;renderer.flush(display,&full,pixels);report(false);
}
static void event(NativeEvent *e)
{
    if(e->target!=renderer.display||e->user_data!=&renderer)return;
    unsigned code=e->code&0x7fff;
    if(code==38){renderer.display=NULL;fail("display-deleted");return;}
    if(code==56) {
        if(renderer.recover)return;
        if(word(renderer.display,84)!=renderer.render_mode||!valid_buffer(pointer(renderer.display,48))||
           !ui_gpu2d_begin(renderer.gpu)){fail("render-begin");return;}
        renderer.started=true;renderer.adapter.enabled=true;
    }
    if(code==57&&renderer.started)fail("missing-final-flush");
}
static void restore(void)
{
    renderer.lock();renderer.adapter.enabled=false;
    if(renderer.adapter.unit&&renderer.adapter.unit->base.dispatch_cb==dispatch)
        renderer.adapter.unit->base.dispatch_cb=renderer.adapter.original;
    if(renderer.display) {
        if(pointer(renderer.display,56)==(void *)flush)
            *(void **)((uint8_t *)renderer.display+56)=(void *)renderer.flush;
        renderer.remove_event(renderer.display,event,&renderer);
        *(uint32_t *)((uint8_t *)renderer.display+84)=1;
        stock_area_t full={0,0,WIDTH-1,HEIGHT-1};
        ((void (*)(void *,const stock_area_t *))(renderer.base+0x55c00u))(renderer.display,&full);
    }
    renderer.attached=false;renderer.unlock();
    ui_gpu2d_destroy(renderer.gpu);renderer.gpu=NULL;
    report(true);
}

/* Called only by the existing hash-gated 2.1.0.157 main-loop integration,
 * outside the native LVGL lock, after installing the complete-frame presenter. */
__attribute__((visibility("default")))
unsigned stock_gpu_tick(uintptr_t base,void *display,unsigned delay)
{
    if(renderer.attached&&renderer.recover){restore();return delay>22000?22000:delay;}
    if(renderer.attempted||!base||!display)return delay;
    renderer.attempted=true;renderer.owner=pthread_self();renderer.base=base;renderer.display=display;
    renderer.lock=(void *)(base+0x3fcf0u);renderer.unlock=(void *)(base+0x3fd10u);
    renderer.remove_event=(void *)(base+0xcb8f0u);
    renderer.lock();
    void *global=pointer((void *)base,STOCK_GLOBAL_GOT_OFFSET);
    stock_draw_sw_unit_t *unit=global?pointer(global,STOCK_UNIT_HEAD_OFFSET):NULL;
    bool valid=global&&pointer(global,0x28)==display&&word(global,STOCK_UNIT_COUNT_OFFSET)==1&&unit&&
        (uintptr_t)unit->base.evaluate_cb==base+STOCK_SW_EVALUATE_OFFSET&&
        word(display,0)==WIDTH&&word(display,4)==HEIGHT&&word(display,84)==1&&
        word(display,92)==STOCK_CF_XRGB8888&&valid_buffer(pointer(display,32))&&
        valid_buffer(pointer(display,40))&&valid_buffer(pointer(display,48))&&pointer(display,752);
    renderer.unlock();
    if(!valid){renderer.reason="unsupported-native-layout";report(true);return delay;}
    renderer.gpu=ui_gpu2d_create(WIDTH,HEIGHT);
    if(!renderer.gpu){renderer.reason="gpu-initialization";report(true);return delay;}
    renderer.lock();
    if(!native_gpu_dispatch_init(&renderer.adapter,base,renderer.gpu,unit,pointer(display,752),false)) {
        renderer.unlock();ui_gpu2d_destroy(renderer.gpu);renderer.gpu=NULL;
        renderer.reason="draw-unit-busy-or-changed";report(true);return delay;
    }
    void *(*add_event)(void *,void (*)(NativeEvent *),uint32_t,void *)=(void *)(base+0xcb7b0u);
    if(!add_event(display,event,0,&renderer)) {
        renderer.unlock();ui_gpu2d_destroy(renderer.gpu);renderer.gpu=NULL;
        renderer.reason="display-observer";report(true);return delay;
    }
    renderer.flush=pointer(display,56);
    *(void **)((uint8_t *)display+56)=(void *)flush;
    renderer.render_mode=1;
#ifdef STOCK_GPU_LAB
    const char *full_repaint=getenv("UI_GPU_FULL_REDRAW");
    if(full_repaint&&!strcmp(full_repaint,"1"))renderer.render_mode=2;
#endif
    *(uint32_t *)((uint8_t *)display+84)=renderer.render_mode;
    unit->base.dispatch_cb=dispatch;renderer.attached=true;
    stock_area_t full={0,0,WIDTH-1,HEIGHT-1};
    ((void (*)(void *,const stock_area_t *))(base+0x55c00u))(display,&full);
    renderer.unlock();report(true);
    return delay>22000?22000:delay;
}
#ifdef STOCK_GPU_LAB
void stock_gpu_lab_fail_next(void)
{
    if(!renderer.attached||renderer.recover)return;
    renderer.inject_failure=true;
    renderer.lock();
    stock_area_t full={0,0,WIDTH-1,HEIGHT-1};
    ((void (*)(void *,const stock_area_t *))(renderer.base+0x55c00u))(renderer.display,&full);
    renderer.unlock();
}
void stock_gpu_lab_report(void)
{
    report(true);
    const char *dir=getenv("NATIVE_GPU_PROBE_DIR");if(!dir)return;
    char path[512];snprintf(path,sizeof(path),"%s/unsupported.jsonl",dir);
    FILE *file=fopen(path,"w");if(!file)return;
    for(unsigned i=0;i<renderer.adapter.rejected_count;++i) {
        fprintf(file,"{\"type\":%u,\"size\":%u,\"hex\":\"",renderer.adapter.rejected[i].type,renderer.adapter.rejected[i].size);
        for(unsigned j=0;j<renderer.adapter.rejected[i].size;++j)fprintf(file,"%02x",renderer.adapter.rejected[i].data[j]);
        fprintf(file,"\"}\n");
    }
    fclose(file);
}
#endif
