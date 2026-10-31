#define _GNU_SOURCE
#include "gpu_output.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct gbm_device;
extern struct gbm_device *gbm_create_device(int);
extern void gbm_device_destroy(struct gbm_device *);
static void require(int condition,const char *message){if(!condition){fprintf(stderr,"%s\n",message);exit(1);}}
static unsigned fd_count(void){
    DIR *directory=opendir("/proc/self/fd");require(directory!=NULL,"fd census failed");
    unsigned count=0;struct dirent *entry;
    while((entry=readdir(directory)))if(entry->d_name[0]!='.')count++;
    closedir(directory);return count;
}
static void check_output(unsigned width,unsigned height,unsigned frame){
    GLint previous;glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
    UiGpuOutput *output=ui_gpu_output_create(width,height);require(output!=NULL,"output import failed");
    GLint current;glGetIntegerv(GL_TEXTURE_BINDING_2D,&current);require(current==previous,"texture binding changed");
    GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,ui_gpu_output_texture(output),0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"import is not renderable");
    size_t bytes=(size_t)width*height*4;uint8_t *reference=malloc(bytes);require(reference!=NULL,"reference allocation failed");
    for(unsigned i=0;i<4;i++){
        glDisable(GL_SCISSOR_TEST);glClearColor((frame%251)/255.f,.3f,.7f,.2f);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,(width+1)/2,(height+1)/2);
        glClearColor(.8f,.2f,.4f,(31+i*53)/255.f);glClear(GL_COLOR_BUFFER_BIT);
        glScissor(width/3,height/3,(width+2)/3,(height+2)/3);
        glClearColor(.25f,.5f,.75f,.4f);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,reference);
        require(glGetError()==GL_NO_ERROR,"reference read failed");
        const uint8_t *mapped=ui_gpu_output_begin_read(output);require(mapped!=NULL,"DMA begin failed");
        require(ui_gpu_output_begin_read(output)==NULL,"nested access accepted");
        for(unsigned y=0;y<height;y++)
            require(!memcmp(mapped+y*ui_gpu_output_stride(output),reference+(size_t)y*width*4,width*4),"RGBA/alpha/orientation differs");
        require(!ui_gpu_output_end_read(output),"DMA end failed");
        require(ui_gpu_output_end_read(output)==-1,"unpaired end accepted");
    }
    free(reference);glDeleteFramebuffers(1,&fbo);ui_gpu_output_destroy(output);
}
int main(void){
    require(ui_gpu_output_create(1,1)==NULL,"created output without EGL context");
    int fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);require(fd>=0,"card open failed");
    struct gbm_device *gbm=gbm_create_device(fd);require(gbm!=NULL,"GBM failed");
    EGLDisplay display=eglGetDisplay(gbm);EGLint major,minor,count=0;EGLConfig config;
    const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,0,EGL_NONE};
    const EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    require(eglInitialize(display,&major,&minor)&&eglBindAPI(EGL_OPENGL_ES_API)&&
            eglChooseConfig(display,attrs,&config,1,&count)&&count,"EGL config failed");
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
    require(context&&eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context),"EGL context failed");
    require(strstr((const char*)glGetString(GL_RENDERER),"Mali")!=NULL,"expected hardware Mali renderer");
    setenv("UI_GPU_ENABLED","0",1);require(ui_gpu_output_create(1,1)==NULL,"disabled GPU ignored");
    setenv("UI_GPU_ENABLED","1",1);setenv("UI_GPU_OUTPUT","readback",1);
    require(ui_gpu_output_create(1,1)==NULL,"baseline override ignored");unsetenv("UI_GPU_OUTPUT");
    require(ui_gpu_output_create(0,1)==NULL,"zero width accepted");
    check_output(480,320,0);unsigned before=fd_count();
    const unsigned sizes[][2]={{1,1},{137,91},{480,320},{959,639}};
    for(unsigned i=0;i<24;i++)check_output(sizes[i%4][0],sizes[i%4][1],i);
    unsigned after=fd_count();require(after==before,"DMA buffer descriptor leak");
    printf("{\"ok\":true,\"renderer\":\"%s\",\"requested_gles_version\":2,\"outputs\":25,"
           "\"frames_per_output\":4,\"rgba_matches_readback\":true,\"alpha_preserved\":true,"
           "\"odd_stride_verified\":true,\"off_and_baseline_verified\":true,\"fds_before\":%u,\"fds_after\":%u}\n",
           glGetString(GL_RENDERER),before,after);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglTerminate(display);gbm_device_destroy(gbm);close(fd);return 0;
}
