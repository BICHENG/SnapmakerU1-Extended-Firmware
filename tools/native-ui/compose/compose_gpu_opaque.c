#define _GNU_SOURCE
#include "compose_gpu.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct gbm_device;
extern struct gbm_device *gbm_create_device(int);
extern void gbm_device_destroy(struct gbm_device *);
enum { REDZONE=4096, ALLOCATION=COMPOSE_BYTES+2*REDZONE, ATLAS=1024, MAX_COMMANDS=512 };
typedef struct { float position[2],uv[2],color[4],masked; } Vertex;
typedef struct { stock_area_t clip; unsigned first,count; int clear; float color[4]; } Group;
struct ComposeGpu {
    int node,fd,cpu_access,current,fetch_extension;
    struct gbm_device *gbm;
    EGLDisplay display;
    EGLContext context;
    EGLImageKHR image;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image;
    GLuint program[2],color,atlas,fbo,vbo,vao;
    uint8_t *mapping;
    Group groups[MAX_COMMANDS];
    unsigned group_count,command_count,unique_masks;
};
static uint64_t clock_ns(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))abort();
    return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
}
static int sync_cpu(ComposeGpu *g,int start,int write) {
    struct dma_buf_sync sync={.flags=(start?DMA_BUF_SYNC_START:DMA_BUF_SYNC_END)|
        (write?DMA_BUF_SYNC_RW:DMA_BUF_SYNC_READ)};
    int r;do{r=ioctl(g->fd,DMA_BUF_IOCTL_SYNC,&sync);}while(r&&(errno==EINTR||errno==EAGAIN));
    return r;
}
static int guards(const ComposeGpu *g) {
    for(size_t i=0;i<REDZONE;i++)
        if(g->mapping[i]!=0xa7||g->mapping[REDZONE+COMPOSE_BYTES+i]!=0xa7)return -1;
    return 0;
}
uint8_t *compose_cpu_begin(ComposeGpu *g) {
    if(g->cpu_access||sync_cpu(g,1,1))return NULL;
    g->cpu_access=2;return g->mapping+REDZONE;
}
int compose_cpu_end(ComposeGpu *g) {
    if(g->cpu_access!=2)return -1;
    int r=guards(g),ended=sync_cpu(g,0,1);g->cpu_access=0;
    return r||ended?-1:0;
}
static void json_string(FILE *file,const char *s) {
    fputc('"',file);
    for(;s&&*s;s++) {
        unsigned char c=(unsigned char)*s;
        if(c=='"'||c=='\\')fputc('\\',file);
        if(c<32)fprintf(file,"\\u%04x",c);else fputc(c,file);
    }
    fputc('"',file);
}
static int extension(const char *name) {
    GLint n=0;glGetIntegerv(GL_NUM_EXTENSIONS,&n);
    for(GLint i=0;i<n;i++)if(!strcmp((const char *)glGetStringi(GL_EXTENSIONS,(GLuint)i),name))return 1;
    return 0;
}
static GLuint compile(GLenum type,const char *source) {
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"compose shader: %s\n",log);glDeleteShader(shader);return 0;}
    return shader;
}
static GLuint make_program(int fetch) {
    const char *vertex="#version 300 es\nprecision highp float;"
        "layout(location=0) in vec2 position;layout(location=1) in vec2 uv;"
        "layout(location=2) in vec4 color;layout(location=3) in float masked;"
        "out vec2 texcoord;flat out vec4 rgba;flat out int use_mask;"
        "void main(){gl_Position=vec4(position/vec2(480.0,320.0)*2.0-1.0,0.0,1.0);"
        "texcoord=uv;rgba=color;use_mask=int(masked);}";
    const char *common="precision highp float;precision highp int;uniform sampler2D masks;"
        "in vec2 texcoord;flat in vec4 rgba;flat in int use_mask;"
        "void main(){uint opa=uint(rgba.a+0.5);if(opa<=2u)discard;"
        "uint a=opa>=253u?255u:opa;if(use_mask!=0){"
        "uint m=uint(texture(masks,texcoord).r*255.0+0.5);a=opa>=253u?m:(m*opa)>>8u;}"
        "if(a==0u)discard;if(a>=253u)a=255u;";
    const char *ending=fetch?
        "uvec3 src=uvec3(rgba.rgb+0.5),dst=uvec3(pixel.rgb*255.0+0.5);"
        "uvec3 out_rgb=a==255u?src:(src*a+dst*(255u-a))>>8u;"
        "pixel=vec4(vec3(out_rgb)/255.0,pixel.a);}":
        "pixel=vec4(rgba.rgb/255.0,float(a)/255.0);}";
    char fragment[2048];
    snprintf(fragment,sizeof(fragment),"#version 300 es\n%s%s%s%s",
        fetch?"#extension GL_EXT_shader_framebuffer_fetch : require\n":"",
        fetch?"layout(location=0) inout highp vec4 pixel;":"layout(location=0) out highp vec4 pixel;",
        common,ending);
    GLuint vs=compile(GL_VERTEX_SHADER,vertex),fs=compile(GL_FRAGMENT_SHADER,fragment);
    if(!vs||!fs){glDeleteShader(vs);glDeleteShader(fs);return 0;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"compose link: %s\n",log);glDeleteProgram(p);return 0;}
    return p;
}
void compose_destroy(ComposeGpu *g) {
    if(!g)return;
    if(g->cpu_access)sync_cpu(g,0,g->cpu_access==2);
    if(g->current){
        glDeleteProgram(g->program[0]);glDeleteProgram(g->program[1]);
        glDeleteBuffers(1,&g->vbo);glDeleteVertexArrays(1,&g->vao);
        glDeleteFramebuffers(1,&g->fbo);glDeleteTextures(1,&g->color);glDeleteTextures(1,&g->atlas);
        if(g->image)g->destroy_image(g->display,g->image);
        eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    }
    if(g->mapping&&g->mapping!=MAP_FAILED)munmap(g->mapping,ALLOCATION);
    if(g->fd>=0)close(g->fd);
    if(g->context)eglDestroyContext(g->display,g->context);
    if(g->display)eglTerminate(g->display);
    if(g->gbm)gbm_device_destroy(g->gbm);
    if(g->node>=0)close(g->node);
    free(g);
}
ComposeGpu *compose_create(char *error,size_t capacity) {
    const char *stage="allocate";ComposeGpu *g=calloc(1,sizeof(*g));if(!g)return NULL;
    g->node=g->fd=-1;
    stage="render node";g->node=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);if(g->node<0)goto fail;
    stage="GBM";g->gbm=gbm_create_device(g->node);if(!g->gbm)goto fail;
    g->display=eglGetDisplay(g->gbm);
    EGLint major,minor,count=0;EGLConfig config;
    const EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,0,EGL_NONE};
    const EGLint context_attributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    stage="EGL";
    if(!eglInitialize(g->display,&major,&minor)||!eglBindAPI(EGL_OPENGL_ES_API)||
       !eglChooseConfig(g->display,attributes,&config,1,&count)||count!=1)goto fail;
    g->context=eglCreateContext(g->display,config,EGL_NO_CONTEXT,context_attributes);
    if(!g->context||!eglMakeCurrent(g->display,EGL_NO_SURFACE,EGL_NO_SURFACE,g->context))goto fail;
    g->current=1;stage="shader";g->program[0]=make_program(0);if(!g->program[0])goto fail;
    g->fetch_extension=extension("GL_EXT_shader_framebuffer_fetch");
    if(g->fetch_extension)g->program[1]=make_program(1);
    PFNEGLCREATEIMAGEKHRPROC create_image=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind_image=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    g->destroy_image=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    stage="DMA import functions";if(!create_image||!bind_image||!g->destroy_image)goto fail;
    int heap=open("/dev/dma_heap/system",O_RDWR|O_CLOEXEC);stage="DMA heap";if(heap<0)goto fail;
    struct dma_heap_allocation_data allocation={.len=ALLOCATION,.fd_flags=O_RDWR|O_CLOEXEC};
    int status=ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&allocation);close(heap);if(status)goto fail;
    g->fd=(int)allocation.fd;g->mapping=mmap(NULL,ALLOCATION,PROT_READ|PROT_WRITE,MAP_SHARED,g->fd,0);
    stage="DMA mmap";if(g->mapping==MAP_FAILED)goto fail;
    if(sync_cpu(g,1,1))goto fail;
    memset(g->mapping,0xa7,ALLOCATION);
    if(sync_cpu(g,0,1))goto fail;
    const EGLint attrs[]={EGL_WIDTH,COMPOSE_W,EGL_HEIGHT,COMPOSE_H,EGL_LINUX_DRM_FOURCC_EXT,0x34325241,
        EGL_DMA_BUF_PLANE0_FD_EXT,g->fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,REDZONE,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,COMPOSE_STRIDE,EGL_NONE};
    stage="ARGB8888 1920-stride import";g->image=create_image(g->display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,attrs);
    if(!g->image)goto fail;
    glGenTextures(1,&g->color);glBindTexture(GL_TEXTURE_2D,g->color);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    bind_image(GL_TEXTURE_2D,g->image);
    glGenFramebuffers(1,&g->fbo);glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g->color,0);
    stage="FBO";if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto fail;
    glGenTextures(1,&g->atlas);glBindTexture(GL_TEXTURE_2D,g->atlas);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenVertexArrays(1,&g->vao);glBindVertexArray(g->vao);glGenBuffers(1,&g->vbo);glBindBuffer(GL_ARRAY_BUFFER,g->vbo);
    const GLint widths[]={2,2,4,1};const size_t offsets[]={0,offsetof(Vertex,uv),offsetof(Vertex,color),offsetof(Vertex,masked)};
    for(unsigned i=0;i<4;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,widths[i],GL_FLOAT,GL_FALSE,sizeof(Vertex),(void *)offsets[i]);}
    if(glGetError()!=GL_NO_ERROR)goto fail;
    return g;
fail:
    snprintf(error,capacity,"%s errno=%d EGL=0x%x GL=0x%x",stage,errno,eglGetError(),g->current?glGetError():0);
    compose_destroy(g);return NULL;
}
int compose_has_fetch(const ComposeGpu *g){return g->program[1]!=0;}
void compose_capabilities(ComposeGpu *g,FILE *report) {
    fputs("{\"renderer\":",report);json_string(report,(const char *)glGetString(GL_RENDERER));
    fputs(",\"version\":",report);json_string(report,(const char *)glGetString(GL_VERSION));
    fputs(",\"extensions\":",report);json_string(report,(const char *)glGetString(GL_EXTENSIONS));
    fprintf(report,",\"ext_fetch_present\":%s,\"ext_fetch_compiled\":%s,\"stride\":1920,\"memory_order\":\"BGRA\",\"guard_bytes_each_end\":4096}",
        g->fetch_extension?"true":"false",compose_has_fetch(g)?"true":"false");
}
int compose_prepare(ComposeGpu *g,const ComposeCommand *c,unsigned count) {
    if(g->cpu_access||!count||count>MAX_COMMANDS)return -1;
    Vertex vertices[MAX_COMMANDS*6];unsigned ax[MAX_COMMANDS],ay[MAX_COMMANDS];
    unsigned x=1,y=1,row_height=0,n=0;g->group_count=0;g->unique_masks=0;g->command_count=count;
    glBindTexture(GL_TEXTURE_2D,g->atlas);glTexImage2D(GL_TEXTURE_2D,0,GL_R8,ATLAS,ATLAS,0,GL_RED,GL_UNSIGNED_BYTE,NULL);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned i=0;i<count;i++) {
        ax[i]=ay[i]=0;
        if(c[i].opacity<=2)continue;
        if(!c[i].mask&&c[i].opacity>=253) {
            Group *clear=&g->groups[g->group_count++];
            clear->clear=1;clear->first=clear->count=0;clear->clip=c[i].clip;
            if(clear->clip.x1<c[i].area.x1)clear->clip.x1=c[i].area.x1;
            if(clear->clip.y1<c[i].area.y1)clear->clip.y1=c[i].area.y1;
            if(clear->clip.x2>c[i].area.x2)clear->clip.x2=c[i].area.x2;
            if(clear->clip.y2>c[i].area.y2)clear->clip.y2=c[i].area.y2;
            clear->color[0]=c[i].red/255.f;clear->color[1]=c[i].green/255.f;
            clear->color[2]=c[i].blue/255.f;clear->color[3]=1.f;
            continue;
        }
        if(c[i].mask) {
            if(!c[i].mask_width||!c[i].mask_height||c[i].mask_stride<c[i].mask_width)return -1;
            unsigned prior=0;
            for(;prior<i;prior++)if(c[prior].mask&&c[prior].mask_width==c[i].mask_width&&
                c[prior].mask_height==c[i].mask_height&&c[prior].mask_stride==c[i].mask_stride&&
                !memcmp(c[prior].mask,c[i].mask,(size_t)c[i].mask_stride*c[i].mask_height))break;
            if(prior<i){ax[i]=ax[prior];ay[i]=ay[prior];}
            else {
                if(c[i].mask_width+2>ATLAS||c[i].mask_height+2>ATLAS)return -1;
                if(x+c[i].mask_width+1>ATLAS){x=1;y+=row_height+1;row_height=0;}
                if(y+c[i].mask_height+1>ATLAS)return -1;
                ax[i]=x;ay[i]=y;glPixelStorei(GL_UNPACK_ROW_LENGTH,(GLint)c[i].mask_stride);
                glTexSubImage2D(GL_TEXTURE_2D,0,(GLint)x,(GLint)y,(GLsizei)c[i].mask_width,(GLsizei)c[i].mask_height,GL_RED,GL_UNSIGNED_BYTE,c[i].mask);
                x+=c[i].mask_width+1;if(c[i].mask_height>row_height)row_height=c[i].mask_height;g->unique_masks++;
            }
        }
        Group *group=g->group_count?&g->groups[g->group_count-1]:NULL;
        if(!group||group->clear||memcmp(&group->clip,&c[i].clip,sizeof(group->clip))){
            group=&g->groups[g->group_count++];group->clip=c[i].clip;group->first=n;group->count=0;group->clear=0;
        }
        static const unsigned corners[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        for(unsigned k=0;k<6;k++) {
            unsigned xx=corners[k][0],yy=corners[k][1];
            vertices[n++]=(Vertex){.position={(float)(xx?c[i].area.x2+1:c[i].area.x1),(float)(yy?c[i].area.y2+1:c[i].area.y1)},
                .uv={(float)(ax[i]+xx*c[i].mask_width)/ATLAS,(float)(ay[i]+yy*c[i].mask_height)/ATLAS},
                .color={c[i].red,c[i].green,c[i].blue,c[i].opacity},.masked=c[i].mask?1.f:0.f};
        }
        group->count+=6;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glBindBuffer(GL_ARRAY_BUFFER,g->vbo);
    glBufferData(GL_ARRAY_BUFFER,n*sizeof(Vertex),vertices,GL_STATIC_DRAW);
    return glGetError()==GL_NO_ERROR?0:-1;
}
int compose_render(ComposeGpu *g,int fetch,uint8_t *pixels,ComposeTiming *timing) {
    if(g->cpu_access||fetch<0||fetch>1||!g->program[fetch])return -1;
    uint64_t start=clock_ns();glBindFramebuffer(GL_FRAMEBUFFER,g->fbo);glViewport(0,0,COMPOSE_W,COMPOSE_H);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if(fetch)glDisable(GL_BLEND);
    else{glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);}
    glUseProgram(g->program[fetch]);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g->atlas);
    glUniform1i(glGetUniformLocation(g->program[fetch],"masks"),0);glBindVertexArray(g->vao);glEnable(GL_SCISSOR_TEST);
    for(unsigned i=0;i<g->group_count;i++) {
        const Group *a=&g->groups[i];int x1=a->clip.x1<0?0:a->clip.x1,y1=a->clip.y1<0?0:a->clip.y1;
        int x2=a->clip.x2>=COMPOSE_W?COMPOSE_W-1:a->clip.x2,y2=a->clip.y2>=COMPOSE_H?COMPOSE_H-1:a->clip.y2;
        if(x2<x1||y2<y1)continue;
        glScissor(x1,y1,x2-x1+1,y2-y1+1);
        if(a->clear){glClearColor(a->color[0],a->color[1],a->color[2],a->color[3]);glClear(GL_COLOR_BUFFER_BIT);}
        else glDrawArrays(GL_TRIANGLES,(GLint)a->first,(GLsizei)a->count);
    }
    uint64_t submitted=clock_ns();glFinish();uint64_t finished=clock_ns();
    if(glGetError()!=GL_NO_ERROR||sync_cpu(g,1,0))return -1;
    g->cpu_access=1;int guard_status=guards(g);memcpy(pixels,g->mapping+REDZONE,COMPOSE_BYTES);
    int sync_status=sync_cpu(g,0,0);g->cpu_access=0;uint64_t end=clock_ns();
    *timing=(ComposeTiming){(submitted-start)/1000.0,(finished-submitted)/1000.0,(end-finished)/1000.0,(end-start)/1000.0};
    return guard_status||sync_status?-1:0;
}
