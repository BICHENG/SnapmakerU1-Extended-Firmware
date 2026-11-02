#define _GNU_SOURCE
#include "gcode-preview.h"
#include "gpu.h"
#include "progress.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Real parser and public runtime API; the GPU gate makes lifecycle races deterministic. */
struct Gpu {pthread_t owner;};
struct ProgressClient {int unused;};
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t caller;
static unsigned created,destroyed,rendered,entered;
static int blocked,fail_render;
static PrintProgress printer={.state=JOB_IDLE};
static const GcodePreviewApi *api;

Gpu *gpu_create(const Model *model,char *error,size_t size){
    (void)error;(void)size;assert(model->layers&&!pthread_equal(caller,pthread_self()));
    Gpu *gpu=malloc(sizeof(*gpu));assert(gpu);gpu->owner=pthread_self();
    pthread_mutex_lock(&gate);created++;pthread_mutex_unlock(&gate);return gpu;
}
int gpu_render(Gpu *gpu,const Model *model,const RenderView *view,uint8_t *pixels){
    assert(pthread_equal(gpu->owner,pthread_self()));
    pthread_mutex_lock(&gate);entered++;pthread_cond_broadcast(&changed);
    while(blocked)pthread_cond_wait(&changed,&gate);
    rendered++;int fail=fail_render;pthread_mutex_unlock(&gate);
    memset(pixels,0,(size_t)view->width*view->height*4);
    pixels[0]=(uint8_t)model->vertices[0].x;pixels[1]=(uint8_t)view->layer;
    pixels[2]=(uint8_t)lroundf(view->zoom*10);pixels[3]=255;
    return fail?-1:0;
}
void gpu_destroy(Gpu *gpu){
    if(!gpu)return;
    assert(pthread_equal(gpu->owner,pthread_self()));
    pthread_mutex_lock(&gate);destroyed++;pthread_mutex_unlock(&gate);free(gpu);
}
ProgressClient *progress_create(void){return calloc(1,sizeof(ProgressClient));}
int progress_poll(ProgressClient *client,PrintProgress *out){
    assert(client&&!pthread_equal(caller,pthread_self()));
    pthread_mutex_lock(&gate);*out=printer;pthread_mutex_unlock(&gate);return 0;
}
void progress_cancel(ProgressClient *client){(void)client;}
void progress_destroy(ProgressClient *client){free(client);}

static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static GcodePreviewStatus status(GcodePreview *p){
    GcodePreviewStatus s;double until=seconds()+3;
    while(api->poll(p,&s,NULL,0)==-1){assert(seconds()<until);usleep(1000);}
    return s;
}
static GcodePreviewStatus phase(GcodePreview *p,int value){
    double until=seconds()+3;GcodePreviewStatus s;
    do{s=status(p);if(s.phase==value)return s;usleep(1000);}while(seconds()<until);
    fprintf(stderr,"phase %d, wanted %d: %s\n",s.phase,value,s.message);abort();
}
static GcodePreviewStatus frame(GcodePreview *p,uint8_t *pixels,int marker,int layer,int zoom){
    double until=seconds()+3;GcodePreviewStatus s;
    do{
        int result=api->poll(p,&s,pixels,GCODE_PREVIEW_MAX_BYTES);
        assert(result!=-2);
        if(result==1){
            assert(pixels[0]==marker);
            if((layer<0||pixels[1]==layer)&&(zoom<0||pixels[2]==zoom))return s;
        }
        usleep(1000);
    }while(seconds()<until);
    fprintf(stderr,"no completed frame for marker %d, phase %d: %s\n",marker,s.phase,s.message);abort();
}
static unsigned hold(void){
    pthread_mutex_lock(&gate);blocked=1;unsigned n=entered;pthread_mutex_unlock(&gate);return n;
}
static void waiting(unsigned prior){
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=3;
    pthread_mutex_lock(&gate);
    while(entered==prior)assert(!pthread_cond_timedwait(&changed,&gate,&until));
    pthread_mutex_unlock(&gate);
}
static void release(void){
    pthread_mutex_lock(&gate);blocked=0;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
}
static void fixture_tools(char *path,int x,unsigned first,unsigned second){
    int fd=mkstemp(path);assert(fd>=0);FILE *f=fdopen(fd,"w");assert(f);
    fprintf(f,"M83\n;LAYER_CHANGE\nG1 X%d Y10 Z.2\nT%u\nG1 X%d E1\n;LAYER_CHANGE\nG1 Z.4\nT%u\nG1 Y20 E1\n",x,first,x+10,second);
    assert(!fclose(f));
}
static void fixture(char *path,int x){fixture_tools(path,x,0,3);}
static void *destroy_session(void *p){api->destroy(p);return NULL;}
static void printing(const char *path){
    struct stat info;assert(!stat(path,&info));
    pthread_mutex_lock(&gate);printer.state=JOB_PRINTING;
    snprintf(printer.filename,sizeof(printer.filename),"%s",strrchr(path,'/')+1);
    printer.file_position=printer.file_size=(uint64_t)info.st_size;pthread_mutex_unlock(&gate);
}
static void source_identity(const char *cached,uint8_t *pixels){
    /* Native job resolution deliberately requires canonical files in this directory. */
    int userdata=mkdir("/userdata",0700)==0;assert(userdata||errno==EEXIST);
    int gcodes=mkdir("/userdata/gcodes",0700)==0;assert(gcodes||errno==EEXIST);
    char a[]="/userdata/gcodes/preview-runtime-a-XXXXXX",b[]="/userdata/gcodes/preview-runtime-b-XXXXXX";
    fixture(a,20);fixture_tools(b,80,1,1);printing(a);
    GcodePreviewSource source={cached,strrchr(a,'/')+1,480,320,0,0};
    GcodePreview *p=api->create();assert(p);assert(!api->load(p,&source));
    GcodePreviewStatus s=frame(p,pixels,20,1,10);
    assert(s.live_available&&s.live_follow&&s.tools_used==9);uint64_t generation=s.generation;
    printing(b);double until=seconds()+3;
    do{
        s=status(p);assert(s.generation==generation&&!strcmp(s.filename,cached)&&s.tools_used==9);
        assert(seconds()<until);usleep(1000);
    }while(s.live_available||s.live_follow);
    s=frame(p,pixels,20,1,10);assert(s.phase==GCODE_PREVIEW_LOADED&&!s.live_follow);
    api->action(p,GCODE_PREVIEW_LAYER,0,0);s=frame(p,pixels,20,0,10);
    assert(!s.following&&!s.live_available&&s.tools_used==9&&!strcmp(s.filename,cached));
    api->action(p,GCODE_PREVIEW_FOLLOW,0,0);assert(!status(p).following);api->destroy(p);

    printing(a);source.path=a;source.job_filename=NULL;
    p=api->create();assert(p);assert(!api->load(p,&source));
    s=frame(p,pixels,20,1,10);assert(s.live_follow);generation=s.generation;
    printing(b);until=seconds()+3;
    do{s=status(p);assert(seconds()<until);usleep(1000);}
    while(s.generation==generation||s.phase!=GCODE_PREVIEW_LOADED);
    s=frame(p,pixels,80,1,10);
    assert(s.generation!=generation&&!strcmp(s.filename,b)&&s.tools_used==2&&s.live_available&&s.live_follow);
    api->destroy(p);unlink(a);unlink(b);
    if(gcodes)rmdir("/userdata/gcodes");
    if(userdata)rmdir("/userdata");
    puts("Explicit source stays pinned; native path-only source follows the next print");
}

int main(void){
    caller=pthread_self();api=gcode_preview_get_api(GCODE_PREVIEW_ABI);
    assert(api&&api->size==sizeof(*api)&&!gcode_preview_get_api(GCODE_PREVIEW_ABI+1));
    setenv("UI_GPU_ENABLED","0",1);assert(!api->create());unsetenv("UI_GPU_ENABLED");
    char first[]="/tmp/gcode-runtime-first-XXXXXX",second[]="/tmp/gcode-runtime-second-XXXXXX";
    fixture(first,20);fixture(second,80);
    GcodePreviewSource source={first,"job.gcode",64,48,1,0};
    GcodePreview *p=api->create();assert(p);api->pause(p,1);assert(!api->load(p,&source));
    GcodePreviewStatus s=phase(p,GCODE_PREVIEW_LOADED);
    assert(s.paused&&!s.visible&&s.layers==2&&s.tools_used==9&&s.tool_colors[3]==0xff563f);
    pthread_mutex_lock(&gate);assert(!created&&!rendered);pthread_mutex_unlock(&gate);
    uint8_t *pixels=malloc(GCODE_PREVIEW_MAX_BYTES);assert(pixels);
    assert(api->poll(p,&s,pixels,GCODE_PREVIEW_MAX_BYTES)==0);
    api->pause(p,0);
    double until=seconds()+3;
    do{s=status(p);assert(seconds()<until);usleep(1000);}while(!s.visible);
    assert(api->poll(p,&s,pixels,1)==-2);
    assert(api->poll(p,&s,NULL,0)==0);
    s=frame(p,pixels,20,1,10);uint64_t first_generation=s.generation;
    assert(api->poll(p,&s,pixels,GCODE_PREVIEW_MAX_BYTES)==0);

    unsigned prior=hold();api->action(p,GCODE_PREVIEW_REFRESH,0,0);waiting(prior);
    double start=seconds();
    for(int i=0;i<1000;i++)api->action(p,GCODE_PREVIEW_ROTATE,.01f,.02f);
    api->action(p,GCODE_PREVIEW_ZOOM,100,0);api->action(p,GCODE_PREVIEW_LAYER,0,0);
    s=status(p);assert(s.zoom==6&&s.tilt==1.4f&&!s.following&&s.layer==0);
    printf("1002 actions and status poll with GPU blocked: %.3f ms\n",(seconds()-start)*1000);
    release();s=frame(p,pixels,20,0,60);
    pthread_mutex_lock(&gate);assert(entered<=prior+2);pthread_mutex_unlock(&gate);
    assert(!api->viewport(p,32,24,1,0)&&api->viewport(p,32,24,0,0)==-1);
    s=frame(p,pixels,20,0,60);assert(s.width==32&&s.height==24);
    api->action(p,GCODE_PREVIEW_FLAT,1,0);s=status(p);assert(s.flat&&s.yaw==0&&s.tilt==0&&s.zoom==6);
    api->action(p,GCODE_PREVIEW_ROTATE,1,1);s=status(p);assert(s.yaw==0&&s.tilt==0);
    api->action(p,GCODE_PREVIEW_ZOOM,.001f,0);s=status(p);assert(s.zoom==.3f);
    api->action(p,GCODE_PREVIEW_RESET,0,0);s=frame(p,pixels,20,0,10);
    assert(!s.flat&&s.yaw==-.45f&&s.tilt==.75f&&!s.following);

    pthread_mutex_lock(&gate);printer.state=JOB_COMPLETE;strcpy(printer.filename,"job.gcode");pthread_mutex_unlock(&gate);
    until=seconds()+3;
    do{s=status(p);assert(seconds()<until);usleep(1000);}while(!s.live_available);
    api->action(p,GCODE_PREVIEW_FOLLOW,0,0);s=frame(p,pixels,20,1,10);assert(s.live_follow&&s.following);
    api->action(p,GCODE_PREVIEW_LAYER,0,0);s=frame(p,pixels,20,0,10);assert(!s.live_follow&&!s.following);

    prior=hold();api->action(p,GCODE_PREVIEW_REFRESH,0,0);waiting(prior);
    source.path=second;assert(!api->load(p,&source));s=status(p);
    assert(s.generation!=first_generation&&!s.visible&&s.phase==GCODE_PREVIEW_LOADING);
    assert(api->poll(p,&s,pixels,GCODE_PREVIEW_MAX_BYTES)==0);
    release();s=frame(p,pixels,80,1,10);assert(s.generation!=first_generation);

    prior=hold();api->action(p,GCODE_PREVIEW_REFRESH,0,0);waiting(prior);
    assert(!api->viewport(p,48,32,1,0));release();
    s=frame(p,pixels,80,1,10);assert(s.width==48&&s.height==32);
    prior=hold();api->action(p,GCODE_PREVIEW_REFRESH,0,0);waiting(prior);
    api->clear(p);release();
    until=seconds()+.05;
    do{s=status(p);assert(s.phase==GCODE_PREVIEW_EMPTY&&!s.visible);
        assert(api->poll(p,&s,pixels,GCODE_PREVIEW_MAX_BYTES)<=0);usleep(1000);}while(seconds()<until);

    source.live_only=1;source.job_filename="other.gcode";assert(!api->load(p,&source));
    s=phase(p,GCODE_PREVIEW_LOADED);assert(!s.visible);
    assert(!api->viewport(p,64,48,1,0));s=frame(p,pixels,80,1,10);
    pthread_mutex_lock(&gate);fail_render=1;pthread_mutex_unlock(&gate);
    api->action(p,GCODE_PREVIEW_REFRESH,0,0);s=phase(p,GCODE_PREVIEW_ERROR);assert(!s.visible&&s.message[0]);
    pthread_mutex_lock(&gate);fail_render=0;pthread_mutex_unlock(&gate);
    source.live_only=0;assert(!api->load(p,&source));s=frame(p,pixels,80,1,10);
    prior=hold();api->action(p,GCODE_PREVIEW_REFRESH,0,0);waiting(prior);
    pthread_t destroyer;assert(!pthread_create(&destroyer,NULL,destroy_session,p));
    usleep(10000);release();assert(!pthread_join(destroyer,NULL));
    source_identity(first,pixels);
    pthread_mutex_lock(&gate);assert(created==destroyed);pthread_mutex_unlock(&gate);
    unlink(first);unlink(second);free(pixels);
    puts("Shared preview lifetime, latest frame, controls and follow checks passed");
    return 0;
}
