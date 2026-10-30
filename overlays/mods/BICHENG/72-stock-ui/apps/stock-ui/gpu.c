#include "gpu.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
extern void stock_log(const char *format,...);
struct gbm_device;
extern struct gbm_device *gbm_create_device(int);
extern void gbm_device_destroy(struct gbm_device *);
struct Gpu {
    int fd,current;
    struct gbm_device *gbm;
    EGLDisplay display;
    EGLContext context;
    GLuint program,texture,fbo,depth,vbo,grid;
    GLint center,scale,rotation;
    uint8_t readback[WIDTH*HEIGHT*4];
};
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
    const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,0,EGL_NONE};
    const EGLint ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
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
        "precision mediump float;varying vec4 c;"
        "void main(){gl_FragColor=vec4(c.b,c.g,c.r,c.a);}");
    if(!vs||!fs){glDeleteShader(vs);glDeleteShader(fs);goto fail;}
    g->program=glCreateProgram();glAttachShader(g->program,vs);glAttachShader(g->program,fs);
    glBindAttribLocation(g->program,0,"position");glBindAttribLocation(g->program,1,"color");
    glLinkProgram(g->program);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok;glGetProgramiv(g->program,GL_LINK_STATUS,&ok);if(!ok)goto fail;
    g->center=glGetUniformLocation(g->program,"center");
    g->scale=glGetUniformLocation(g->program,"scale");
    g->rotation=glGetUniformLocation(g->program,"rotation");
    glGenTextures(1,&g->texture);glBindTexture(GL_TEXTURE_2D,g->texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,WIDTH,HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&g->fbo);glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g->texture,0);
    glGenRenderbuffers(1,&g->depth);glBindRenderbuffer(GL_RENDERBUFFER,g->depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,WIDTH,HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,g->depth);
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
int gpu_render(Gpu *g,const Model *m,unsigned layer,int flat,float yaw,float tilt,float zoom,uint8_t *pixels) {
    if(layer>=m->layers)layer=m->layers-1;
    glViewport(0,0,WIDTH,HEIGHT);glDisable(GL_SCISSOR_TEST);
    glClearColor(0.07f,0.065f,0.06f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glUseProgram(g->program);
    float span=fmaxf(fmaxf(m->max[0]-m->min[0],m->max[1]-m->min[1]),m->max[2]-m->min[2]);
    if(span<1)span=1;
    glUniform3f(g->center,(m->min[0]+m->max[0])/2,(m->min[1]+m->max[1])/2,(m->min[2]+m->max[2])/2);
    float dx=m->max[0]-m->min[0],dy=m->max[1]-m->min[1],dz=m->max[2]-m->min[2];
    float pixels_per_mm=(flat?fminf(420/fmaxf(dx,1),190/fmaxf(dy,1)):
                             190/fmaxf(sqrtf(dx*dx+dy*dy+dz*dz),1))*zoom;
    glUniform3f(g->scale,2*pixels_per_mm/WIDTH,2*pixels_per_mm/HEIGHT,0.8f/span);
    glUniform4f(g->rotation,cosf(yaw),sinf(yaw),cosf(tilt),sinf(tilt));
    attributes(g->grid);glLineWidth(1);glDrawArrays(GL_LINES,0,84);
    attributes(g->vbo);glLineWidth(flat?2:1);
    size_t first=flat&&layer?m->layer_end[layer-1]:0;
    glDrawArrays(GL_LINES,first,m->layer_end[layer]-first);
    glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,g->readback);
    if(glGetError()!=GL_NO_ERROR)return -1;
    for(int y=0;y<HEIGHT;y++)memcpy(pixels+y*WIDTH*4,g->readback+(HEIGHT-1-y)*WIDTH*4,WIDTH*4);
    for(int y=0;y<HEIGHT;y++)if(y<48||y>=264)for(int x=0;x<WIDTH;x++){
        uint8_t *p=pixels+(y*WIDTH+x)*4;p[0]=25;p[1]=23;p[2]=20;p[3]=255;
    }
    int fill=60+(m->layers>1?(int)(360ull*layer/(m->layers-1)):360);
    for(int y=270;y<274;y++)for(int x=60;x<=420;x++){
        uint8_t *p=pixels+(y*WIDTH+x)*4;
        p[0]=x<=fill?255:65;p[1]=x<=fill?102:60;p[2]=x<=fill?24:55;
    }
    return 0;
}
