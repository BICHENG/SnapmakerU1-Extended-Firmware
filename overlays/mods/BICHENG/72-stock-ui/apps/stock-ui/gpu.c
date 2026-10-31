#include "gpu.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#ifdef STOCK_GPU_PROFILE
#include <time.h>
static double clock_ms(void){
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
}
#endif
extern void stock_log(const char *format,...);
struct gbm_device;
extern struct gbm_device *gbm_create_device(int);
extern void gbm_device_destroy(struct gbm_device *);
#define SAMPLES 2
struct Gpu {
    int fd,current;
    struct gbm_device *gbm;
    EGLDisplay display;
    EGLContext context;
    GLuint program,texture,fbo,depth,vbo,grid,resolve_fbo,resolve_color;
    GLuint composite,scene_fbo,scene_texture;
    GLint center,scale,rotation,brightness,extent;
    uint8_t readback[WIDTH*HEIGHT*4];
#ifdef STOCK_GPU_PROFILE
    GpuProfile *profile;
#endif
};
#ifdef STOCK_GPU_PROFILE
void gpu_profile(Gpu *g,GpuProfile *profile){g->profile=profile;}
#endif
static GLuint shader(GLenum kind,const char *source) {
    GLuint s=glCreateShader(kind);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){glDeleteShader(s);return 0;}return s;
}
void gpu_destroy(Gpu *g) {
    if(!g)return;
    if(g->display&&g->context){
        if(g->current){
            glDeleteBuffers(1,&g->vbo);glDeleteBuffers(1,&g->grid);
            glDeleteRenderbuffers(1,&g->depth);glDeleteTextures(1,&g->texture);
            glDeleteFramebuffers(1,&g->fbo);glDeleteProgram(g->program);
            glDeleteFramebuffers(1,&g->resolve_fbo);glDeleteRenderbuffers(1,&g->resolve_color);
            glDeleteProgram(g->composite);glDeleteFramebuffers(1,&g->scene_fbo);glDeleteTextures(1,&g->scene_texture);
            eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        }
        eglDestroyContext(g->display,g->context);
    }
    if(g->display)eglTerminate(g->display);
    if(g->gbm)gbm_device_destroy(g->gbm);
    if(g->fd>=0)close(g->fd);
    free(g);
}
Gpu *gpu_create(const Model *m,char *error,size_t size) {
    Gpu *g=calloc(1,sizeof(*g));if(!g){snprintf(error,size,"内存不足");return NULL;}
    g->fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
    if(g->fd<0||!(g->gbm=gbm_create_device(g->fd)))goto fail;
    g->display=eglGetDisplay(g->gbm);
    EGLint major,minor,n;EGLConfig config;
    const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,0,EGL_NONE};
    const EGLint ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    if(!eglInitialize(g->display,&major,&minor)||!eglBindAPI(EGL_OPENGL_ES_API)||
       !eglChooseConfig(g->display,attrs,&config,1,&n)||!n)goto fail;
    g->context=eglCreateContext(g->display,config,EGL_NO_CONTEXT,ctxattrs);
    if(!g->context||!eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,g->context))goto fail;
    g->current=1;
    GLuint vs=shader(GL_VERTEX_SHADER,
        "attribute vec3 position;attribute vec4 color;varying vec4 c;"
        "uniform vec3 center;uniform vec3 scale;uniform vec4 rotation;"
        "void main(){vec3 p=position-center;"
        "float x=p.x*rotation.x-p.y*rotation.y;"
        "float y=p.x*rotation.y+p.y*rotation.x;"
        "float v=y*rotation.z+p.z*rotation.w;"
        "float d=-y*rotation.w+p.z*rotation.z;"
        "gl_Position=vec4(x*scale.x,v*scale.y,-d*scale.z,1.0);c=color;}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,
        "precision mediump float;varying vec4 c;uniform float brightness;"
        "void main(){gl_FragColor=vec4(c.bgr*brightness,c.a);}");
    if(!vs||!fs){glDeleteShader(vs);glDeleteShader(fs);goto fail;}
    g->program=glCreateProgram();glAttachShader(g->program,vs);glAttachShader(g->program,fs);
    glBindAttribLocation(g->program,0,"position");glBindAttribLocation(g->program,1,"color");
    glLinkProgram(g->program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok;glGetProgramiv(g->program,GL_LINK_STATUS,&ok);if(!ok)goto fail;
    g->center=glGetUniformLocation(g->program,"center");
    g->scale=glGetUniformLocation(g->program,"scale");
    g->rotation=glGetUniformLocation(g->program,"rotation");
    g->brightness=glGetUniformLocation(g->program,"brightness");
    vs=shader(GL_VERTEX_SHADER,
        "#version 300 es\nuniform vec2 extent;out vec2 uv;"
        "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "uv=p*extent;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}");
    fs=shader(GL_FRAGMENT_SHADER,
        "#version 300 es\nprecision mediump float;uniform sampler2D source;in vec2 uv;out vec4 color;"
        "void main(){color=texture(source,uv);color.a*=0.24;}");
    if(!vs||!fs){glDeleteShader(vs);glDeleteShader(fs);goto fail;}
    g->composite=glCreateProgram();glAttachShader(g->composite,vs);glAttachShader(g->composite,fs);
    glLinkProgram(g->composite);glDeleteShader(vs);glDeleteShader(fs);
    glGetProgramiv(g->composite,GL_LINK_STATUS,&ok);if(!ok)goto fail;
    g->extent=glGetUniformLocation(g->composite,"extent");
    glGenTextures(1,&g->texture);glBindTexture(GL_TEXTURE_2D,g->texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,WIDTH*SAMPLES,HEIGHT*SAMPLES,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&g->fbo);glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g->texture,0);
    glGenRenderbuffers(1,&g->depth);glBindRenderbuffer(GL_RENDERBUFFER,g->depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,WIDTH*SAMPLES,HEIGHT*SAMPLES);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,g->depth);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto fail;
    glGenTextures(1,&g->scene_texture);glBindTexture(GL_TEXTURE_2D,g->scene_texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,WIDTH*SAMPLES,HEIGHT*SAMPLES,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&g->scene_fbo);glBindFramebuffer(GL_FRAMEBUFFER,g->scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g->scene_texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto fail;
    glGenFramebuffers(1,&g->resolve_fbo);glBindFramebuffer(GL_FRAMEBUFFER,g->resolve_fbo);
    glGenRenderbuffers(1,&g->resolve_color);glBindRenderbuffer(GL_RENDERBUFFER,g->resolve_color);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,WIDTH,HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,g->resolve_color);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto fail;
    glGenBuffers(1,&g->vbo);glBindBuffer(GL_ARRAY_BUFFER,g->vbo);
    glBufferData(GL_ARRAY_BUFFER,m->count*sizeof(Vertex),m->vertices,GL_STATIC_DRAW);
    Vertex grid[84];unsigned at=0;
    float extent=fmaxf(m->max[0]-m->min[0],m->max[1]-m->min[1])*0.65f;
    float cx=(m->max[0]+m->min[0])/2,cy=(m->max[1]+m->min[1])/2;
    for(int i=-10;i<=10;i++)for(int axis=0;axis<2;axis++)for(int end=0;end<2;end++){
        Vertex *v=&grid[at++];
        v->x=cx+(axis?i*extent/10:(end?extent:-extent));
        v->y=cy+(axis?(end?extent:-extent):i*extent/10);
        v->z=m->min[2]-0.1f;memcpy(v->color,(uint8_t[]){45,51,58,255},4);
    }
    glGenBuffers(1,&g->grid);glBindBuffer(GL_ARRAY_BUFFER,g->grid);
    glBufferData(GL_ARRAY_BUFFER,sizeof(grid),grid,GL_STATIC_DRAW);
    if(glGetError()!=GL_NO_ERROR)goto fail;
    stock_log("renderer=%s vertices=%zu layers=%u\n",glGetString(GL_RENDERER),m->count,m->layers);
    return g;
fail:
    snprintf(error,size,"GPU 预览初始化失败");
    gpu_destroy(g);return NULL;
}
static void attributes(GLuint buffer) {
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,x));
    glVertexAttribPointer(1,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Vertex),(void*)offsetof(Vertex,color));
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
}
static void context_paths(size_t first,size_t end,size_t count) {
    glDrawArrays(GL_LINES,0,first);
    glDrawArrays(GL_LINES,end,count-end);
}
int gpu_render(Gpu *g,const Model *m,const RenderView *v,uint8_t *pixels) {
    unsigned layer=v->layer;int flat=v->flat,w=v->width,h=v->height;
    if(w<1||w>WIDTH||h<1||h>HEIGHT)return -1;
#ifdef STOCK_GPU_PROFILE
    double draw_start=0,readback_start=0,copy_start=0;
    if(g->profile){glFinish();draw_start=clock_ms();}
#endif
    float yaw=v->yaw,tilt=v->tilt,zoom=v->zoom;
    if(layer>=m->layers)layer=m->layers-1;
    glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);
    glViewport(0,0,w*SAMPLES,h*SAMPLES);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_BLEND);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glUseProgram(g->program);
    float span=fmaxf(fmaxf(m->max[0]-m->min[0],m->max[1]-m->min[1]),m->max[2]-m->min[2]);
    if(span<1)span=1;
    glUniform3f(g->center,(m->min[0]+m->max[0])/2,(m->min[1]+m->max[1])/2,(m->min[2]+m->max[2])/2);
    float dx=m->max[0]-m->min[0],dy=m->max[1]-m->min[1],dz=m->max[2]-m->min[2];
    float fit_w=v->compact?w*.90f:452,fit_h=v->compact?h*.90f:256;
    float pixels_per_mm=(flat?fminf(fit_w/fmaxf(dx,1),fit_h/fmaxf(dy,1)):
                             fminf(fit_w,fit_h)/fmaxf(sqrtf(dx*dx+dy*dy+dz*dz),1))*zoom;
    glUniform3f(g->scale,2*pixels_per_mm/w,2*pixels_per_mm/h,0.8f/span);
    glUniform4f(g->rotation,cosf(yaw),sinf(yaw),cosf(tilt),sinf(tilt));
    glUniform1f(g->brightness,1);
    attributes(g->vbo);glLineWidth(SAMPLES);
    size_t first=layer?m->layer_end[layer-1]:0;
    size_t end=m->layer_end[layer];
    /* Composite the context once so stacked layers cannot accumulate opacity. */
    context_paths(first,end,m->count);
    glBindFramebuffer(GL_FRAMEBUFFER,g->scene_fbo);glDisable(GL_DEPTH_TEST);
    glClearColor(0.07f,0.065f,0.06f,1);glClear(GL_COLOR_BUFFER_BIT);
    attributes(g->grid);glDrawArrays(GL_LINES,0,84);
    glUseProgram(g->composite);glUniform2f(g->extent,(float)w/WIDTH,(float)h/HEIGHT);
    glBindTexture(GL_TEXTURE_2D,g->texture);
    glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,3);
    glDisable(GL_BLEND);glUseProgram(g->program);attributes(g->vbo);
    glLineWidth(SAMPLES*(flat?1.75f:1.5f));
    size_t completed=v->completed<end?v->completed:end;
    if(completed<first)completed=first;
    glUniform1f(g->brightness,.38f);glDrawArrays(GL_LINES,completed,end-completed);
    glUniform1f(g->brightness,1);glDrawArrays(GL_LINES,first,completed-first);
#ifdef STOCK_GPU_PROFILE
    if(g->profile){
        glFinish();readback_start=clock_ms();
        g->profile->draw_ms=readback_start-draw_start;
    }
#endif
    glBindFramebuffer(GL_READ_FRAMEBUFFER,g->scene_fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,g->resolve_fbo);
    glBlitFramebuffer(0,0,w*SAMPLES,h*SAMPLES,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,g->resolve_fbo);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,g->readback);
    if(glGetError()!=GL_NO_ERROR)return -1;
#ifdef STOCK_GPU_PROFILE
    if(g->profile){
        copy_start=clock_ms();
        g->profile->resolve_readback_ms=copy_start-readback_start;
    }
#endif
    for(int y=0;y<h;y++)memcpy(pixels+y*w*4,g->readback+(h-1-y)*w*4,w*4);
    if(!v->compact){
        int fill=12+(m->layers>1?(int)(456ull*layer/(m->layers-1)):456);
        for(int y=308;y<312;y++)for(int x=12;x<=468;x++){
            uint8_t *p=pixels+(y*WIDTH+x)*4;
            p[0]=x<=fill?255:65;p[1]=x<=fill?102:60;p[2]=x<=fill?24:55;
        }
        for(int y=304;y<=315;y++)for(int x=fill-6;x<=fill+6;x++){
            float alpha=fmaxf(0,fminf(1,6-sqrtf((x-fill)*(x-fill)+(y-309.5f)*(y-309.5f))));
            uint8_t *p=pixels+(y*WIDTH+x)*4;
            p[0]=(uint8_t)(p[0]*(1-alpha)+255*alpha);
            p[1]=(uint8_t)(p[1]*(1-alpha)+102*alpha);
            p[2]=(uint8_t)(p[2]*(1-alpha)+24*alpha);
        }
    }
#ifdef STOCK_GPU_PROFILE
    if(g->profile)g->profile->cpu_prepare_ms=clock_ms()-copy_start;
#endif
    return 0;
}
