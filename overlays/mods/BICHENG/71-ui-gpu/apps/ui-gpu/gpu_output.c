#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gpu_output.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct UiGpuOutput {
    EGLDisplay display;
    EGLContext context;
    EGLImageKHR image;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image;
    GLuint texture;
    int fd,reading;
    size_t stride,bytes;
    uint8_t *pixels;
};

static int dma_sync(UiGpuOutput *output,uint64_t flags)
{
    struct dma_buf_sync sync={.flags=flags};int result;
    do{result=ioctl(output->fd,DMA_BUF_IOCTL_SYNC,&sync);}while(result&&(errno==EINTR||errno==EAGAIN));
    return result;
}

void ui_gpu_output_destroy(UiGpuOutput *output)
{
    if(!output)return;
    if(output->reading)dma_sync(output,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ);
    glDeleteTextures(1,&output->texture);
    if(output->image)output->destroy_image(output->display,output->image);
    if(output->pixels)munmap(output->pixels,output->bytes);
    if(output->fd>=0)close(output->fd);
    free(output);
}

UiGpuOutput *ui_gpu_output_create(unsigned width,unsigned height)
{
    const char *enabled=getenv("UI_GPU_ENABLED"),*mode=getenv("UI_GPU_OUTPUT");
    if((enabled&&!strcmp(enabled,"0"))||(mode&&!strcmp(mode,"readback")))return NULL;
    EGLDisplay display=eglGetCurrentDisplay();EGLContext context=eglGetCurrentContext();
    if(display==EGL_NO_DISPLAY||context==EGL_NO_CONTEXT||!width||!height)return NULL;
    GLint maximum=0,old_texture=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    if(maximum<=0||width>(unsigned)maximum||height>(unsigned)maximum)return NULL;
    const char *extensions=eglQueryString(display,EGL_EXTENSIONS);
    if(!extensions||!strstr(extensions,"EGL_EXT_image_dma_buf_import"))return NULL;
    PFNEGLCREATEIMAGEKHRPROC create_image=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind_image=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    PFNEGLDESTROYIMAGEKHRPROC destroy_image=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if(!create_image||!bind_image||!destroy_image)return NULL;
    UiGpuOutput *output=calloc(1,sizeof(*output));if(!output)return NULL;
    output->display=display;output->context=context;output->destroy_image=destroy_image;output->fd=-1;
    output->stride=((size_t)width*4+63)&~(size_t)63;
    if(output->stride>INT32_MAX||height>(SIZE_MAX-4095)/output->stride)goto fail;
    output->bytes=(output->stride*height+4095)&~(size_t)4095;
    int heap=open("/dev/dma_heap/system",O_RDWR|O_CLOEXEC);if(heap<0)goto fail;
    struct dma_heap_allocation_data allocation={.len=output->bytes,.fd_flags=O_RDWR|O_CLOEXEC};
    int status;do{status=ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&allocation);}while(status&&errno==EINTR);
    close(heap);if(status)goto fail;
    output->fd=(int)allocation.fd;
    void *mapping=mmap(NULL,output->bytes,PROT_READ|PROT_WRITE,MAP_SHARED,output->fd,0);
    if(mapping==MAP_FAILED)goto fail;
    output->pixels=mapping;
    /* DRM ABGR8888 is R,G,B,A in memory, matching GL_RGBA/GL_UNSIGNED_BYTE. */
    const EGLint attrs[]={EGL_WIDTH,(EGLint)width,EGL_HEIGHT,(EGLint)height,
        EGL_LINUX_DRM_FOURCC_EXT,0x34324241,EGL_DMA_BUF_PLANE0_FD_EXT,output->fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)output->stride,EGL_NONE};
    output->image=create_image(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,attrs);
    if(!output->image)goto fail;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_texture);
    glGenTextures(1,&output->texture);glBindTexture(GL_TEXTURE_2D,output->texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    bind_image(GL_TEXTURE_2D,output->image);
    GLenum error=glGetError();glBindTexture(GL_TEXTURE_2D,(GLuint)old_texture);
    if(error!=GL_NO_ERROR)goto fail;
    return output;
fail:
    ui_gpu_output_destroy(output);return NULL;
}

unsigned ui_gpu_output_texture(const UiGpuOutput *output){return output->texture;}
size_t ui_gpu_output_stride(const UiGpuOutput *output){return output->stride;}

const uint8_t *ui_gpu_output_begin_read(UiGpuOutput *output)
{
    if(!output||output->reading||eglGetCurrentContext()!=output->context||
       eglGetCurrentDisplay()!=output->display)return NULL;
    glFinish();
    if(glGetError()!=GL_NO_ERROR||dma_sync(output,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ))return NULL;
    output->reading=1;return output->pixels;
}

int ui_gpu_output_end_read(UiGpuOutput *output)
{
    if(!output||!output->reading)return -1;
    if(dma_sync(output,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ))return -1;
    output->reading=0;return 0;
}
