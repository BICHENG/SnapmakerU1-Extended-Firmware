// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_gpu_renderer.h"
#include "gpu_output.h"
#include "src/core/lv_global.h"
#include "src/display/lv_display_private.h"
#include "src/draw/nanovg/lv_draw_nanovg.h"
#include "src/draw/nanovg/lv_draw_nanovg_private.h"
#include "src/draw/nanovg/lv_nanovg_utils.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct {
    lv_display_t *display;
    lv_draw_nanovg_unit_t *unit;
    lv_draw_buf_t *owned_buffer;
    lv_timer_t *recovery;
    void (*flush)(lv_display_t *,const lv_area_t *,uint8_t *);
    int32_t (*gpu_dispatch)(lv_draw_unit_t *,lv_layer_t *);
    int32_t (*cpu_dispatch)(lv_draw_unit_t *,lv_layer_t *);
    int32_t (*gpu_delete)(lv_draw_unit_t *);
    void (*gpu_event)(lv_event_t *);
    struct gbm_device *gbm;
    EGLDisplay egl;
    EGLContext context;
    EGLSurface surface;
    UiGpuOutput *output;
    GLuint fbo,stencil;
    int fd;
    bool active,failed;
    unsigned frames,gpu_tasks,cpu_tasks,rejected_frames;
    uint32_t last_report;
    const char *reason;
} renderer={.fd=-1};

static void release_context(void)
{
    if(renderer.context && renderer.egl) {
        eglMakeCurrent(renderer.egl,renderer.surface,renderer.surface,renderer.context);
        glDeleteRenderbuffers(1,&renderer.stencil);
        glDeleteFramebuffers(1,&renderer.fbo);
        ui_gpu_output_destroy(renderer.output);
        eglMakeCurrent(renderer.egl,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        eglDestroyContext(renderer.egl,renderer.context);
    }
    if(renderer.surface)eglDestroySurface(renderer.egl,renderer.surface);
    if(renderer.egl)eglTerminate(renderer.egl);
    if(renderer.gbm)gbm_device_destroy(renderer.gbm);
    if(renderer.fd>=0)close(renderer.fd);
    if(renderer.owned_buffer)lv_draw_buf_destroy(renderer.owned_buffer);
    memset(&renderer,0,sizeof(renderer));renderer.fd=-1;
}

static bool create_context(void)
{
    renderer.fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);
    if(renderer.fd<0 || !(renderer.gbm=gbm_create_device(renderer.fd)))return false;
    renderer.egl=eglGetDisplay(renderer.gbm);
    EGLint major,minor,count;EGLConfig config;
    const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_STENCIL_SIZE,8,EGL_NONE};
    if(!eglInitialize(renderer.egl,&major,&minor)||!eglBindAPI(EGL_OPENGL_ES_API)||
       !eglChooseConfig(renderer.egl,attrs,&config,1,&count)||!count)return false;
    renderer.context=eglCreateContext(renderer.egl,config,EGL_NO_CONTEXT,
        (EGLint[]){EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE});
    renderer.surface=eglCreatePbufferSurface(renderer.egl,config,
        (EGLint[]){EGL_WIDTH,480,EGL_HEIGHT,320,EGL_NONE});
    if(!renderer.context||!renderer.surface||
       !eglMakeCurrent(renderer.egl,renderer.surface,renderer.surface,renderer.context))return false;
    renderer.output=ui_gpu_output_create(480,320,UI_GPU_BGRA);
    if(!renderer.output)return false;
    glGenFramebuffers(1,&renderer.fbo);glBindFramebuffer(GL_FRAMEBUFFER,renderer.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
        ui_gpu_output_texture(renderer.output),0);
    glGenRenderbuffers(1,&renderer.stencil);glBindRenderbuffer(GL_RENDERBUFFER,renderer.stencil);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,480,320);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,renderer.stencil);
    glViewport(0,0,480,320);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE&&glGetError()==GL_NO_ERROR;
}

static void report(void)
{
    uint32_t now=lv_tick_get();
    if(renderer.last_report&&now-renderer.last_report<5000)return;
    renderer.last_report=now;
    char path[]="/tmp/ui-renderer.XXXXXX";
    int fd=mkstemp(path);if(fd<0)return;
    FILE *file=fdopen(fd,"w");
    if(!file){close(fd);unlink(path);return;}
    fprintf(file,"{\"pid\":%ld,\"ui\":\"helix\",\"renderer\":\"%s\",\"frames\":%u,\"gpu_tasks\":%u,\"cpu_tasks\":%u,\"rejected_frames\":%u,\"reason\":\"%s\"}\n",
        (long)getpid(),renderer.active?"nanovg-gles3":"software-recovery",renderer.frames,renderer.gpu_tasks,
        renderer.cpu_tasks,renderer.rejected_frames,renderer.reason?renderer.reason:"");
    if(fclose(file)==0)rename(path,"/tmp/ui-renderer.json");
    unlink(path);
}

static int32_t dispatch_gpu(lv_draw_unit_t *unit,lv_layer_t *layer)
{
    if(!renderer.active)return LV_DRAW_UNIT_IDLE;
    int32_t result=renderer.gpu_dispatch(unit,layer);
    if(result>0)renderer.gpu_tasks+=result;
    return result;
}

static int32_t dispatch_cpu(lv_draw_unit_t *unit,lv_layer_t *layer)
{
    int32_t result=renderer.cpu_dispatch(unit,layer);
    if(result>0) {
        renderer.cpu_tasks+=result;
        if(renderer.active){renderer.failed=true;renderer.reason="unsupported-draw-task";}
    }
    return result;
}

static int32_t decline(lv_draw_unit_t *unit,lv_draw_task_t *task)
{
    LV_UNUSED(unit);LV_UNUSED(task);return 0;
}

static void gpu_event(lv_event_t *event)
{
    if(renderer.active||lv_event_get_code(event)==LV_EVENT_CHILD_DELETED)
        renderer.gpu_event(event);
    if(renderer.active&&lv_event_get_code(event)==LV_EVENT_CHILD_CREATED) {
        lv_layer_t *layer=lv_event_get_param(event);
        if(!layer->user_data){renderer.failed=true;renderer.reason="layer-allocation";}
    }
}

static int32_t delete_gpu(lv_draw_unit_t *unit)
{
    eglMakeCurrent(renderer.egl,renderer.surface,renderer.surface,renderer.context);
    int32_t result=renderer.gpu_delete(unit);
    release_context();
    return result;
}

static void recover(lv_timer_t *timer)
{
    LV_UNUSED(timer);renderer.recovery=NULL;
    if(renderer.display)lv_obj_invalidate(lv_display_get_screen_active(renderer.display));
}

static void display_event(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_DELETE){renderer.display=NULL;return;}
    if(lv_event_get_code(event)!=LV_EVENT_RENDER_START||!renderer.active)return;
    if(!eglMakeCurrent(renderer.egl,renderer.surface,renderer.surface,renderer.context)) {
        renderer.active=false;renderer.reason="context-lost";
        renderer.unit->base_unit.evaluate_cb=decline;
        renderer.last_report=0;report();return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,renderer.fbo);glViewport(0,0,480,320);
}

static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels)
{
    lv_area_t complete={0,0,479,319};
    if(renderer.active) {
        bool valid=area&&area->x1>=0&&area->y1>=0&&area->x1<=area->x2&&area->y1<=area->y2&&area->x2<480&&area->y2<320;
        lv_draw_buf_t *buffer=lv_display_get_buf_active(display);
        valid=valid&&buffer&&buffer->data==pixels&&buffer->header.stride>=1920&&buffer->data_size>=buffer->header.stride*320u;
        if(!valid){renderer.failed=true;renderer.reason="display-layout-changed";}
        if(!lv_display_flush_is_last(display)){lv_display_flush_ready(display);return;}
        lv_nanovg_end_frame(renderer.unit);
        if(!renderer.failed) {
            const uint8_t *source=ui_gpu_output_begin_read(renderer.output);
            if(!source){renderer.failed=true;renderer.reason="gpu-output";}
            else {
                for(unsigned y=0;y<320;++y)
                    memcpy(pixels+y*buffer->header.stride,source+(319-y)*ui_gpu_output_stride(renderer.output),1920);
                if(ui_gpu_output_end_read(renderer.output)||glGetError()!=GL_NO_ERROR) {
                    renderer.failed=true;renderer.reason="gpu-readback";
                }
            }
        }
        if(renderer.failed) {
            renderer.active=false;renderer.unit->base_unit.evaluate_cb=decline;
            ++renderer.rejected_frames;
            renderer.recovery=lv_timer_create(recover,1,NULL);
            if(renderer.recovery)lv_timer_set_repeat_count(renderer.recovery,1);
            renderer.last_report=0;report();
            /* The partially rendered frame never reaches the panel or the remote sink. */
            lv_display_flush_ready(display);return;
        }
        ++renderer.frames;
        area=&complete;
    }
    renderer.flush(display,area,pixels);report();
}

bool ui_gpu_renderer_attach(lv_display_t *display)
{
    const char *enabled=getenv("UI_GPU_ENABLED");
    if(!enabled||strcmp(enabled,"1")||renderer.display||!display||
       lv_display_get_horizontal_resolution(display)!=480||lv_display_get_vertical_resolution(display)!=320||
       lv_display_get_rotation(display)!=LV_DISPLAY_ROTATION_0||
       lv_display_get_color_format(display)!=LV_COLOR_FORMAT_ARGB8888)return false;
    lv_draw_unit_t *software=LV_GLOBAL_DEFAULT()->draw_info.unit_head;
    if(!software||software->next||!display->flush_cb)return false;
    if(!create_context()){release_context();return false;}
    if(lv_display_get_render_mode(display)==LV_DISPLAY_RENDER_MODE_PARTIAL) {
        renderer.owned_buffer=lv_draw_buf_create(480,320,LV_COLOR_FORMAT_ARGB8888,LV_STRIDE_AUTO);
        if(!renderer.owned_buffer){release_context();return false;}
    }
    lv_draw_nanovg_init();
    renderer.unit=(lv_draw_nanovg_unit_t *)LV_GLOBAL_DEFAULT()->draw_info.unit_head;
    if(!renderer.unit||!renderer.unit->base_unit.name||strcmp(renderer.unit->base_unit.name,"NANOVG")) {
        release_context();return false;
    }
    renderer.display=display;renderer.flush=display->flush_cb;renderer.active=true;
    renderer.gpu_dispatch=renderer.unit->base_unit.dispatch_cb;
    renderer.gpu_delete=renderer.unit->base_unit.delete_cb;
    renderer.gpu_event=renderer.unit->base_unit.event_cb;
    renderer.cpu_dispatch=software->dispatch_cb;
    renderer.unit->base_unit.dispatch_cb=dispatch_gpu;renderer.unit->base_unit.delete_cb=delete_gpu;
    renderer.unit->base_unit.event_cb=gpu_event;software->dispatch_cb=dispatch_cpu;
    if(renderer.owned_buffer)lv_display_set_draw_buffers(display,renderer.owned_buffer,NULL);
    const char *full=getenv("UI_GPU_FULL_REDRAW");
    lv_display_set_render_mode(display,full&&!strcmp(full,"1")?LV_DISPLAY_RENDER_MODE_FULL:LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display,flush);
    lv_display_add_event_cb(display,display_event,LV_EVENT_ALL,NULL);
    report();return true;
}
