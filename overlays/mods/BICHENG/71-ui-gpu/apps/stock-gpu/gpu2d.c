#define _GNU_SOURCE
#include "gpu2d.h"
#include "gpu_output.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "src/libs/nanovg/nanovg.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "src/libs/nanovg/nanovg_gl.h"

struct gbm_device;
extern struct gbm_device *gbm_create_device(int);
extern void gbm_device_destroy(struct gbm_device *);

enum { CACHE_IMAGES=512, CACHE_BYTES=8*1024*1024 };
typedef struct { uint64_t hash,used; int width,height,format,image; size_t bytes; } CachedImage;
typedef struct { const void *key; int width,height,image; GLuint fbo,stencil; } LayerImage;
typedef struct { EGLDisplay display; EGLContext context; EGLSurface draw,read; } EglState;
static EglState egl_state(void) {
    return (EglState){eglGetCurrentDisplay(),eglGetCurrentContext(),
        eglGetCurrentSurface(EGL_DRAW),eglGetCurrentSurface(EGL_READ)};
}
static bool egl_restore(EglState state,EGLDisplay own) {
    return state.context?eglMakeCurrent(state.display,state.draw,state.read,state.context):
        eglMakeCurrent(own,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
}
struct UiGpu2D {
    int width,height,node;
    bool current,started,reading;
    struct gbm_device *gbm;
    EGLDisplay display; EGLContext context;
    GLuint fbo,stencil;
    UiGpuOutput *output;
    NVGcontext *vg;
    CachedImage images[CACHE_IMAGES];
    LayerImage layers[8];
    const void *target;
    size_t cached_bytes;
    size_t layer_bytes;
    uint64_t frame;
    EglState previous;
};

void ui_gpu2d_destroy(UiGpu2D *g) {
    if(!g)return;
    EglState previous=egl_state();
    if(previous.context==g->context)previous=(EglState){0};
    if(g->current) {
        eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,g->context);
        for(unsigned i=0;i<8;++i) {
            glDeleteFramebuffers(1,&g->layers[i].fbo);
            glDeleteRenderbuffers(1,&g->layers[i].stencil);
        }
        if(g->vg)nvgDeleteGLES3(g->vg);
        glDeleteRenderbuffers(1,&g->stencil);glDeleteFramebuffers(1,&g->fbo);
        ui_gpu_output_destroy(g->output);
        egl_restore(previous,g->display);
    }
    if(g->context)eglDestroyContext(g->display,g->context);
    if(g->display)eglTerminate(g->display);
    if(g->gbm)gbm_device_destroy(g->gbm);
    if(g->node>=0)close(g->node);
    free(g);
}
UiGpu2D *ui_gpu2d_create(int width,int height) {
    if(width<=0||height<=0||width>2048||height>2048)return NULL;
    UiGpu2D *g=calloc(1,sizeof(*g));if(!g)return NULL;
    EglState previous=egl_state();
    g->width=width;g->height=height;g->node=-1;
    g->node=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);if(g->node<0)goto fail;
    g->gbm=gbm_create_device(g->node);if(!g->gbm)goto fail;
    g->display=eglGetDisplay(g->gbm);
    EGLint major,minor,count;EGLConfig config;
    const EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,0,EGL_NONE};
    if(!eglInitialize(g->display,&major,&minor)||!eglBindAPI(EGL_OPENGL_ES_API)||
       !eglChooseConfig(g->display,attributes,&config,1,&count)||count!=1)goto fail;
    g->context=eglCreateContext(g->display,config,EGL_NO_CONTEXT,(EGLint[]){EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE});
    if(!g->context||!eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,g->context))goto fail;
    g->current=true;
    g->output=ui_gpu_output_create(width,height,UI_GPU_BGRA);if(!g->output)goto fail;
    glGenFramebuffers(1,&g->fbo);glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,ui_gpu_output_texture(g->output),0);
    glGenRenderbuffers(1,&g->stencil);glBindRenderbuffer(GL_RENDERBUFFER,g->stencil);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,width,height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,g->stencil);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto fail;
    g->vg=nvgCreateGLES3(NVG_ANTIALIAS);if(!g->vg)goto fail;
    glViewport(0,0,width,height);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    if(glGetError()!=GL_NO_ERROR)goto fail;
    if(!egl_restore(previous,g->display))goto fail;
    return g;
fail:
    fprintf(stderr,"GPU2D initialization failed: errno=%d EGL=0x%x GL=0x%x\n",errno,eglGetError(),g->current?glGetError():0);
    { EGLDisplay display=g->display;
      ui_gpu2d_destroy(g);if(previous.context)egl_restore(previous,display); }
    return NULL;
}
bool ui_gpu2d_begin(UiGpu2D *g) {
    if(!g||g->started||g->reading)return false;
    g->previous=egl_state();
    if(!eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,g->context))return false;
    glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);glViewport(0,0,g->width,g->height);
    nvgBeginFrame(g->vg,g->width,g->height,1);
    /* Direct BGRA DMA output: no CPU color conversion or vertical flip. */
    nvgTranslate(g->vg,0,(float)g->height);nvgScale(g->vg,1,-1);
    g->target=NULL;
    for(unsigned i=0;i<8;++i)g->layers[i].key=NULL;
    g->started=true;++g->frame;return true;
}
bool ui_gpu2d_target(UiGpu2D *g,const void *key,UiGpu2DArea area) {
    if(!g||!g->started||g->reading)return false;
    if(key==g->target)return true;
    int width=key?area.x2-area.x1+1:g->width,height=key?area.y2-area.y1+1:g->height;
    if(width<=0||height<=0||width>2048||height>2048||(size_t)width*height>480*320*4)return false;
    nvgEndFrame(g->vg);
    GLuint fbo=g->fbo;
    bool clear=false;
    if(key) {
        LayerImage *layer=NULL,*spare=NULL;
        for(unsigned i=0;i<8;++i) {
            LayerImage *item=&g->layers[i];
            if(item->key==key){layer=item;break;}
            if(!item->key&&(!spare||(item->width==width&&item->height==height)))spare=item;
        }
        if(!layer) {
            layer=spare;if(!layer)return false;
            if(layer->image&&(layer->width!=width||layer->height!=height)) {
                glDeleteFramebuffers(1,&layer->fbo);glDeleteRenderbuffers(1,&layer->stencil);
                g->layer_bytes-=(size_t)layer->width*layer->height*8;
                nvgDeleteImage(g->vg,layer->image);*layer=(LayerImage){0};
            }
            if(!layer->image) {
                size_t required=(size_t)width*height*8;
                for(unsigned i=0;g->layer_bytes+required>CACHE_BYTES&&i<8;++i) {
                    LayerImage *old=&g->layers[i];
                    if(old==layer||old->key||!old->image)continue;
                    glDeleteFramebuffers(1,&old->fbo);glDeleteRenderbuffers(1,&old->stencil);
                    g->layer_bytes-=(size_t)old->width*old->height*8;
                    nvgDeleteImage(g->vg,old->image);*old=(LayerImage){0};
                }
                if(g->layer_bytes+required>CACHE_BYTES)return false;
                layer->image=nvgCreateImage(g->vg,width,height,NVG_IMAGE_PREMULTIPLIED,NVG_TEXTURE_RGBA,NULL);
                if(!layer->image)return false;
                layer->width=width;layer->height=height;
                g->layer_bytes+=required;
                glGenFramebuffers(1,&layer->fbo);glBindFramebuffer(GL_FRAMEBUFFER,layer->fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
                    nvglImageHandleGLES3(g->vg,layer->image),0);
                glGenRenderbuffers(1,&layer->stencil);glBindRenderbuffer(GL_RENDERBUFFER,layer->stencil);
                glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,width,height);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,layer->stencil);
                if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
            }
            layer->key=key;clear=true;
        }
        if(layer->width!=width||layer->height!=height)return false;
        fbo=layer->fbo;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,width,height);
    if(clear) {
        glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glStencilMask(~0u);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    }
    nvgBeginFrame(g->vg,width,height,1);
    nvgTranslate(g->vg,0,(float)height);nvgScale(g->vg,1,-1);
    if(key)nvgTranslate(g->vg,-area.x1,-area.y1);
    g->target=key;return glGetError()==GL_NO_ERROR;
}
static bool clip(UiGpu2D *g,UiGpu2DArea a) {
    if(!g||!g->started||a.x1>a.x2||a.y1>a.y2)return false;
    nvgScissor(g->vg,a.x1,a.y1,a.x2-a.x1+1,a.y2-a.y1+1);return true;
}
bool ui_gpu2d_layer(UiGpu2D *g,const void *key,UiGpu2DArea a,UiGpu2DArea c,unsigned opacity) {
    if(!key||!clip(g,c)||a.x1>a.x2||a.y1>a.y2)return false;
    LayerImage *layer=NULL;
    for(unsigned i=0;i<8;++i)if(g->layers[i].key==key){layer=&g->layers[i];break;}
    if(!layer)return false;
    if(opacity>2) {
        float w=a.x2-a.x1+1,h=a.y2-a.y1+1;
        NVGpaint paint=nvgImagePattern(g->vg,a.x1,a.y1,w,h,0,layer->image,(opacity>=253?255:opacity)/255.f);
        nvgShapeAntiAlias(g->vg,0);nvgBeginPath(g->vg);nvgRect(g->vg,a.x1,a.y1,w,h);
        nvgFillPaint(g->vg,paint);nvgFill(g->vg);
    }
    layer->key=NULL;return true;
}
bool ui_gpu2d_fill(UiGpu2D *g,UiGpu2DArea a,UiGpu2DArea c,int radius,uint32_t rgb,unsigned opacity) {
    if(!clip(g,c)||a.x1>a.x2||a.y1>a.y2)return false;
    if(opacity<=2)return true;
    nvgShapeAntiAlias(g->vg,1);
    nvgBeginPath(g->vg);
    nvgRoundedRect(g->vg,a.x1,a.y1,a.x2-a.x1+1,a.y2-a.y1+1,radius>0?radius:0);
    nvgFillColor(g->vg,nvgRGBA((rgb>>16)&255,(rgb>>8)&255,rgb&255,opacity>=253?255:opacity));
    nvgFill(g->vg);return true;
}
bool ui_gpu2d_line(UiGpu2D *g,UiGpu2DArea c,float x1,float y1,float x2,float y2,
                  int width,unsigned caps,uint32_t rgb,unsigned opacity) {
    if(!clip(g,c)||!isfinite(x1)||!isfinite(x2)||!isfinite(y1)||!isfinite(y2)||
       fabsf(x1)>32767||fabsf(x2)>32767||fabsf(y1)>32767||fabsf(y2)>32767||width<0||width>2048)return false;
    if(!width||opacity<=2||(x1==x2&&y1==y2))return true;
    NVGcolor color=nvgRGBA((rgb>>16)&255,(rgb>>8)&255,rgb&255,opacity>=253?255:opacity);
    int low=(width-1)/2,high=width/2;
    if(y1==y2||x1==x2) {
        nvgShapeAntiAlias(g->vg,0);nvgBeginPath(g->vg);
        if(y1==y2)nvgRect(g->vg,(int)fminf(x1,x2),(int)y1-high,(int)fmaxf(x1,x2)-(int)fminf(x1,x2),width);
        else nvgRect(g->vg,(int)x1-high,(int)fminf(y1,y2),width,(int)fmaxf(y1,y2)-(int)fminf(y1,y2));
        nvgFillColor(g->vg,color);nvgFill(g->vg);
    }
    else {
        nvgShapeAntiAlias(g->vg,1);nvgBeginPath(g->vg);
        nvgMoveTo(g->vg,x1+.5f,y1+.5f);nvgLineTo(g->vg,x2+.5f,y2+.5f);
        nvgLineCap(g->vg,NVG_BUTT);nvgStrokeWidth(g->vg,width);nvgStrokeColor(g->vg,color);nvgStroke(g->vg);
    }
    for(unsigned i=0;i<2;++i)if(caps&(1u<<i)) {
        int x=i?x2:x1,y=i?y2:y1;
        if(!ui_gpu2d_fill(g,(UiGpu2DArea){x-width/2,y-width/2,x+low,y+low},c,width,rgb,opacity))return false;
    }
    return true;
}
bool ui_gpu2d_arc(UiGpu2D *g,UiGpu2DArea c,int x,int y,int radius,int width,
                 float start,float end,bool rounded,uint32_t rgb,unsigned opacity) {
    if(!clip(g,c)||radius<0||width<0||radius>2048||!isfinite(start)||!isfinite(end))return false;
    if(!width||!radius||opacity<=2||start==end)return true;
    if(width>radius)width=radius;
    bool full=fabsf(end-start)>=360;
    start=fmodf(start,360);end=fmodf(end,360);
    if(start<0)start+=360;
    if(end<0)end+=360;
    if(end<start)end+=360;
    nvgShapeAntiAlias(g->vg,1);nvgBeginPath(g->vg);
    const float r=radius-width*.5f;
    if(full)nvgCircle(g->vg,x,y,r);
    else nvgArc(g->vg,x,y,r,start*(NVG_PI/180),end*(NVG_PI/180),NVG_CW);
    nvgLineCap(g->vg,rounded?NVG_ROUND:NVG_BUTT);nvgStrokeWidth(g->vg,width);
    nvgStrokeColor(g->vg,nvgRGBA((rgb>>16)&255,(rgb>>8)&255,rgb&255,opacity>=253?255:opacity));
    nvgStroke(g->vg);return true;
}
bool ui_gpu2d_bitmap(UiGpu2D *g,UiGpu2DArea a,UiGpu2DArea c,const uint8_t *pixels,
                    int width,int height,int stride,unsigned format,uint32_t rgb,unsigned opacity,const float matrix[6]) {
    unsigned bpp=format==0x0e?1:format==0x12?2:format==0x0f?3:(format==0x10||format==0x11)?4:0;
    if(!clip(g,c)||!pixels||!bpp||width<=0||height<=0||width>2048||height>2048||
       stride<width*(int)bpp||(size_t)width*height*bpp>CACHE_BYTES)return false;
    if(opacity<=2)return true;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(int y=0;y<height;++y)for(unsigned x=0;x<(unsigned)width*bpp;++x)hash=(hash^pixels[y*stride+x])*UINT64_C(1099511628211);
    CachedImage *found=NULL;
    for(unsigned i=0;i<CACHE_IMAGES;++i) {
        CachedImage *item=&g->images[i];
        if(item->image&&item->hash==hash&&item->width==width&&item->height==height&&item->format==(int)format){found=item;break;}
    }
    if(!found) {
        size_t length=(size_t)width*height*bpp;
        bool submitted=false;
        while(!found||g->cached_bytes+length>CACHE_BYTES) {
            CachedImage *oldest=NULL,*empty=NULL;
            for(unsigned i=0;i<CACHE_IMAGES;++i) {
                CachedImage *item=&g->images[i];
                if(!item->image) {if(!empty)empty=item;}
                else if(!oldest||item->used<oldest->used)oldest=item;
            }
            if(empty&&g->cached_bytes+length<=CACHE_BYTES){found=empty;break;}
            if(!oldest)return false;
            /* Submit once before evicting any texture referenced by this frame. */
            if(!submitted){nvgEndFrame(g->vg);submitted=true;}
            nvgDeleteImage(g->vg,oldest->image);g->cached_bytes-=oldest->bytes;
            *oldest=(CachedImage){0};
        }
        uint8_t *packed=NULL;
        if(stride!=width*(int)bpp) {
            packed=malloc(length);if(!packed)return false;
            for(int y=0;y<height;++y)memcpy(packed+(size_t)y*width*bpp,pixels+(size_t)y*stride,(size_t)width*bpp);
        }
        int type=bpp==1?NVG_TEXTURE_ALPHA:bpp==2?NVG_TEXTURE_RGB565:bpp==3?NVG_TEXTURE_BGR:
            format==0x11?NVG_TEXTURE_BGRX:NVG_TEXTURE_BGRA;
        int image=nvgCreateImage(g->vg,width,height,0,type,packed?packed:pixels);free(packed);
        if(!image)return false;
        *found=(CachedImage){.hash=hash,.width=width,.height=height,.format=(int)format,.image=image,.bytes=length};
        g->cached_bytes+=length;
    }
    found->used=g->frame;
    nvgSave(g->vg);
    if(matrix)nvgTransform(g->vg,matrix[0],matrix[1],matrix[2],matrix[3],matrix[4],matrix[5]);
    if(opacity>=253)opacity=255;
    float w=a.x2-a.x1+1,h=a.y2-a.y1+1;
    NVGpaint paint=nvgImagePattern(g->vg,a.x1,a.y1,w,h,0,found->image,opacity/255.f);
    if(bpp==1)paint.innerColor=paint.outerColor=nvgRGBA((rgb>>16)&255,(rgb>>8)&255,rgb&255,opacity);
    nvgShapeAntiAlias(g->vg,0);
    nvgBeginPath(g->vg);nvgRect(g->vg,a.x1,a.y1,w,h);nvgFillPaint(g->vg,paint);nvgFill(g->vg);nvgRestore(g->vg);return true;
}
bool ui_gpu2d_border(UiGpu2D *g,UiGpu2DArea a,UiGpu2DArea c,int radius,int width,unsigned sides,uint32_t rgb,unsigned opacity) {
    if(!clip(g,c)||a.x1>a.x2||a.y1>a.y2||width<0||(sides&~31u))return false;
    sides&=15;
    if(!width||!sides||opacity<=2)return true;
    int w=a.x2-a.x1+1,h=a.y2-a.y1+1;
    if(radius>0&&sides!=15)return false;
    nvgShapeAntiAlias(g->vg,radius>0);nvgBeginPath(g->vg);
    if(radius>0) {
        float r=radius;
        if(r>w*.5f)r=w*.5f;
        if(r>h*.5f)r=h*.5f;
        nvgRoundedRect(g->vg,a.x1,a.y1,w,h,r);
        if(w>2*width&&h>2*width) {
            nvgRoundedRect(g->vg,a.x1+width,a.y1+width,w-2*width,h-2*width,r>width?r-width:0);
            nvgPathWinding(g->vg,NVG_HOLE);
        }
    }
    else {
        int top=sides&2?(width<h?width:h):0;
        int bottom=sides&1?(width<h-top?width:h-top):0;
        int left=sides&4?(width<w?width:w):0;
        int right=sides&8?(width<w-left?width:w-left):0;
        if(top)nvgRect(g->vg,a.x1,a.y1,w,top);
        if(bottom)nvgRect(g->vg,a.x1,a.y2-bottom+1,w,bottom);
        if(h>top+bottom) {
            if(left)nvgRect(g->vg,a.x1,a.y1+top,left,h-top-bottom);
            if(right)nvgRect(g->vg,a.x2-right+1,a.y1+top,right,h-top-bottom);
        }
    }
    nvgFillColor(g->vg,nvgRGBA((rgb>>16)&255,(rgb>>8)&255,rgb&255,opacity>=253?255:opacity));
    nvgFill(g->vg);return true;
}
bool ui_gpu2d_shadow(UiGpu2D *g,UiGpu2DArea a,UiGpu2DArea c,int radius,int width,int spread,int dx,int dy,uint32_t rgb,unsigned opacity) {
    if(!clip(g,c)||width<0||width>512||radius<0||abs(spread)>512||abs(dx)>4096||abs(dy)>4096)return false;
    if(width<=1||opacity<=2)return true;
    float x=a.x1+dx-spread,y=a.y1+dy-spread,w=a.x2-a.x1+1+spread*2,h=a.y2-a.y1+1+spread*2;
    if(w<=0||h<=0)return true;
    float r=radius;if(r>w*.5f)r=w*.5f;if(r>h*.5f)r=h*.5f;
    NVGcolor inner=nvgRGBA((rgb>>16)&255,(rgb>>8)&255,rgb&255,opacity>=253?255:opacity);
    NVGpaint paint=nvgBoxGradient(g->vg,x,y,w,h,r,width,inner,nvgRGBA(0,0,0,0));
    float padding=width*.5f+1;
    nvgShapeAntiAlias(g->vg,0);nvgBeginPath(g->vg);
    nvgRect(g->vg,x-padding,y-padding,w+2*padding,h+2*padding);
    nvgFillPaint(g->vg,paint);nvgFill(g->vg);return true;
}
const uint8_t *ui_gpu2d_finish(UiGpu2D *g) {
    if(!g||!g->started||g->reading)return NULL;
    if(g->target&&!ui_gpu2d_target(g,NULL,(UiGpu2DArea){0,0,g->width-1,g->height-1}))return NULL;
    nvgEndFrame(g->vg);g->started=false;
    const uint8_t *pixels=ui_gpu_output_begin_read(g->output);
    if(!pixels)return NULL;
    g->reading=true;return pixels;
}
bool ui_gpu2d_read_end(UiGpu2D *g) {
    if(!g||!g->reading)return false;
    bool ok=ui_gpu_output_end_read(g->output)==0;g->reading=false;
    return egl_restore(g->previous,g->display)&&ok;
}
