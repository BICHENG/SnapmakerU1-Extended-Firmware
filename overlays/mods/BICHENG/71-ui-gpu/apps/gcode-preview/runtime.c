#define _GNU_SOURCE
#include "gcode-preview.h"
#include "gpu.h"
#include "progress.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct GcodePreview {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    atomic_uint generation;
    int stop,wanted,frame_ready,live_only;
    uint64_t viewport_revision;
    char path[4096],job_filename[4096];
    struct stat file;
    GcodePreviewStatus status;
    RenderView frame_view;
    float frame_height;
    uint8_t frames[2][GCODE_PREVIEW_MAX_BYTES],*pixels;
};

static uint64_t nanoseconds(void){
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
void stock_log(const char *format,...){
    FILE *f=fopen("/tmp/gcode-preview.log","a");if(!f)return;
    struct stat st;
    if(!fstat(fileno(f),&st)&&st.st_size>65536){fclose(f);f=fopen("/tmp/gcode-preview.log","w");if(!f)return;}
    va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);fclose(f);
}
static void request(GcodePreview *p){p->wanted=1;pthread_cond_signal(&p->changed);}
static void discard(GcodePreview *p){
    atomic_fetch_add(&p->generation,1);
    p->status.layers=0;p->status.layer=0;p->status.layer_height=0;p->status.tools_used=0;
    p->status.visible=0;p->status.live_available=0;p->status.live_follow=0;
    p->status.message[0]=0;p->frame_ready=0;
    p->status.phase=p->path[0]?GCODE_PREVIEW_LOADING:GCODE_PREVIEW_EMPTY;
    request(p);
}
static int valid_view(uint32_t width,uint32_t height,int compact){
    return width>=1&&width<=WIDTH&&height>=1&&height<=HEIGHT&&
           (compact||(width==WIDTH&&height==HEIGHT));
}
static int progress_path(const PrintProgress *progress,char *path,size_t size){
    if(!progress->filename[0])return 0;
    char candidate[4096],real[4096];
    int n=snprintf(candidate,sizeof(candidate),"/userdata/gcodes/%s",progress->filename);
    if(n<0||(size_t)n>=sizeof(candidate)||!realpath(candidate,real)||
       strncmp(real,"/userdata/gcodes/",17))return 0;
    snprintf(path,size,"%s",real);return 1;
}
static void *worker(void *context){
    GcodePreview *p=context;
    Model model={0};Gpu *gpu=NULL;unsigned loaded=0;
    ProgressClient *client=NULL;PrintProgress progress={.state=JOB_OFFLINE};
    uint64_t queried=0;int offline=1,polling=0,previous_follow=0,gpu_failed=0;
    size_t completed=0;uint8_t *pixels=p->frames[1];
    for(;;){
        pthread_mutex_lock(&p->mutex);
        while(!p->stop&&!p->wanted){
            if(p->path[0]&&!p->status.paused){
                unsigned interval=progress.state==JOB_PRINTING||progress.state==JOB_PAUSED?250:1000;
                uint64_t elapsed=nanoseconds()/1000000-queried;
                unsigned delay=polling?22:elapsed>=interval?1:interval-(unsigned)elapsed;
                struct timespec until;clock_gettime(CLOCK_MONOTONIC,&until);
                until.tv_nsec+=(long)delay*1000000;
                if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
                if(pthread_cond_timedwait(&p->changed,&p->mutex,&until)==ETIMEDOUT)break;
            }else pthread_cond_wait(&p->changed,&p->mutex);
        }
        if(p->stop){pthread_mutex_unlock(&p->mutex);break;}
        int redraw=p->wanted;p->wanted=0;
        unsigned generation=atomic_load(&p->generation);
        uint64_t viewport_revision=p->viewport_revision;
        int follow=p->status.following,paused=p->status.paused,live_only=p->live_only;
        RenderView view={.layer=p->status.layer,.flat=p->status.flat,
            .width=(int)p->status.width,.height=(int)p->status.height,.compact=p->status.compact,
            .yaw=p->status.yaw,.tilt=p->status.tilt,.zoom=p->status.zoom,.completed=SIZE_MAX};
        char path[4096],job_filename[4096];
        snprintf(path,sizeof(path),"%s",p->path);
        snprintf(job_filename,sizeof(job_filename),"%s",p->job_filename);
        pthread_mutex_unlock(&p->mutex);
        if(!path[0]){
            gpu_destroy(gpu);gpu=NULL;gcode_free(&model);loaded=0;gpu_failed=0;
            progress_cancel(client);polling=0;previous_follow=0;completed=0;continue;
        }
        if(loaded!=generation){
            gpu_destroy(gpu);gpu=NULL;gpu_failed=0;completed=0;previous_follow=0;
            int result=gcode_load(&model,path,&p->generation,generation);
            if(atomic_load(&p->generation)!=generation)continue;
            loaded=generation;
            pthread_mutex_lock(&p->mutex);
            if(atomic_load(&p->generation)==generation){
                p->status.phase=result?GCODE_PREVIEW_ERROR:GCODE_PREVIEW_LOADED;
                p->status.layers=model.layers;p->status.layer=model.layers?model.layers-1:0;
                p->status.tools_used=model.tools_used;view.layer=p->status.layer;
                p->status.layer_height=model.layers?model.layer_z[view.layer]:0;
                snprintf(p->status.message,sizeof(p->status.message),"%s",result?model.error:"");
                paused=p->status.paused;
            }
            pthread_mutex_unlock(&p->mutex);redraw=1;
        }
        if(paused){progress_cancel(client);polling=0;continue;}
        uint64_t now=nanoseconds()/1000000;
        unsigned interval=progress.state==JOB_PRINTING||progress.state==JOB_PAUSED?250:1000;
        if(polling||!queried||now-queried>=interval){
            PrintProgress next;
            if(!client)client=progress_create();
            if(!polling)queried=now;
            int result=progress_poll(client,&next);polling=result==1;
            if(!result){
                if(offline||next.state!=progress.state||strcmp(next.filename,progress.filename)||
                   next.file_position!=progress.file_position||next.file_size!=progress.file_size)redraw=1;
                progress=next;offline=0;
            }else if(result<0){if(!offline)redraw=1;offline=1;}
        }
        char printing_path[4096]={0};
        int known=progress_path(&progress,printing_path,sizeof(printing_path));
        int matches=(job_filename[0]?!strcmp(job_filename,progress.filename):known&&!strcmp(path,printing_path))&&
            (progress.state==JOB_PRINTING||progress.state==JOB_PAUSED||progress.state==JOB_COMPLETE);
        if(!job_filename[0]&&!live_only&&follow&&previous_follow&&known&&progress.state==JOB_PRINTING&&!matches){
            pthread_mutex_lock(&p->mutex);
            if(generation==atomic_load(&p->generation)){
                snprintf(p->path,sizeof(p->path),"%s",printing_path);p->job_filename[0]=0;
                memset(&p->file,0,sizeof(p->file));discard(p);
            }
            pthread_mutex_unlock(&p->mutex);continue;
        }
        int tracking=matches&&follow,run=!live_only||matches;
        pthread_mutex_lock(&p->mutex);
        if(generation==atomic_load(&p->generation)){
            p->status.live_available=matches;
            p->status.live_follow=matches&&p->status.following;p->status.offline=offline;
            if(!run){p->status.visible=0;p->frame_ready=0;}
            paused=p->status.paused;
        }
        pthread_mutex_unlock(&p->mutex);
        if(atomic_load(&p->generation)!=generation||paused)continue;
        if(!run){previous_follow=0;continue;}
        if(!gpu&&!gpu_failed&&model.layers&&!model.error[0]){
            gpu=gpu_create(&model,model.error,sizeof(model.error));redraw=1;
            if(!gpu){
                gpu_failed=1;pthread_mutex_lock(&p->mutex);
                if(generation==atomic_load(&p->generation)){
                    p->status.phase=GCODE_PREVIEW_ERROR;p->status.visible=0;p->frame_ready=0;
                    snprintf(p->status.message,sizeof(p->status.message),"%s",model.error);
                }
                pthread_mutex_unlock(&p->mutex);
            }
        }
        if(tracking&&gpu){
            if(completed>model.count)completed=model.count;
            if(!offline&&progress.state==JOB_COMPLETE){completed=model.count;view.layer=model.layers-1;}
            else if(!offline&&(progress.state==JOB_PRINTING||progress.state==JOB_PAUSED))
                gcode_progress(&model,progress.file_position,&completed,&view.layer);
            else {size_t ignored;gcode_progress(&model,completed?model.line_end[completed/2-1]:0,&ignored,&view.layer);}
            view.completed=completed;
        }
        if(tracking!=previous_follow)redraw=1;
        previous_follow=tracking;
        if(gpu&&redraw&&atomic_load(&p->generation)==generation){
            int result=gpu_render(gpu,&model,&view,pixels);
            if(result){gpu_destroy(gpu);gpu=NULL;gpu_failed=1;}
            pthread_mutex_lock(&p->mutex);
            if(generation==atomic_load(&p->generation)){
                if(result){
                    p->status.phase=GCODE_PREVIEW_ERROR;p->status.visible=0;p->frame_ready=0;
                    snprintf(p->status.message,sizeof(p->status.message),"GPU 绘制失败");
                }else if(viewport_revision==p->viewport_revision&&!p->status.paused){
                    uint8_t *previous=p->pixels;p->pixels=pixels;pixels=previous;p->frame_ready=1;
                    p->status.visible=1;p->status.message[0]=0;
                    if(tracking&&p->status.following)p->status.layer=view.layer;
                    p->status.layer_height=model.layer_z[p->status.layer<model.layers?p->status.layer:model.layers-1];
                    p->frame_view=view;p->frame_height=model.layer_z[view.layer];
                }
            }
            pthread_mutex_unlock(&p->mutex);
        }
    }
    gpu_destroy(gpu);gcode_free(&model);progress_destroy(client);return NULL;
}

static GcodePreview *preview_create(void){
    const char *enabled=getenv("UI_GPU_ENABLED");
    if(enabled&&!strcmp(enabled,"0")){errno=ENODEV;return NULL;}
    GcodePreview *p=calloc(1,sizeof(*p));if(!p)return NULL;atomic_init(&p->generation,0);
    p->pixels=p->frames[0];
    pthread_condattr_t condattr;pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr,CLOCK_MONOTONIC);
    pthread_mutex_init(&p->mutex,NULL);pthread_cond_init(&p->changed,&condattr);pthread_condattr_destroy(&condattr);
    p->status.width=WIDTH;p->status.height=HEIGHT;p->status.following=1;p->status.offline=1;
    p->status.yaw=-.45f;p->status.tilt=.75f;p->status.zoom=1;
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,512*1024);
    int error=pthread_create(&p->thread,&attr,worker,p);pthread_attr_destroy(&attr);
    if(error){pthread_cond_destroy(&p->changed);pthread_mutex_destroy(&p->mutex);free(p);errno=error;return NULL;}
    return p;
}
static void preview_destroy(GcodePreview *p){
    if(!p)return;
    pthread_mutex_lock(&p->mutex);p->stop=1;atomic_fetch_add(&p->generation,1);request(p);pthread_mutex_unlock(&p->mutex);
    pthread_join(p->thread,NULL);pthread_cond_destroy(&p->changed);pthread_mutex_destroy(&p->mutex);free(p);
}
static void viewport(GcodePreview *p,uint32_t width,uint32_t height,int compact,int live_only){
    if(p->status.width!=width||p->status.height!=height||p->status.compact!=!!compact||p->live_only!=!!live_only){
        p->status.width=width;p->status.height=height;p->status.compact=!!compact;p->live_only=!!live_only;
        p->viewport_revision++;p->frame_ready=0;p->status.visible=0;request(p);
    }
}
static int same_file(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
           a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
           a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int preview_load(GcodePreview *p,const GcodePreviewSource *source){
    if(!p||!source||!source->path||!valid_view(source->width,source->height,source->compact))return -1;
    char path[4096];struct stat info;
    if(!realpath(source->path,path)||stat(path,&info)||!S_ISREG(info.st_mode)||access(path,R_OK))return -1;
    if(source->job_filename&&strlen(source->job_filename)>=sizeof(p->job_filename))return -1;
    pthread_mutex_lock(&p->mutex);
    const char *job=source->job_filename?source->job_filename:"";
    if(!strcmp(p->path,path)&&!strcmp(p->job_filename,job)&&same_file(&p->file,&info)&&
       p->status.phase!=GCODE_PREVIEW_ERROR){
        viewport(p,source->width,source->height,source->compact,source->live_only);
        pthread_mutex_unlock(&p->mutex);return 0;
    }
    snprintf(p->path,sizeof(p->path),"%s",path);
    snprintf(p->job_filename,sizeof(p->job_filename),"%s",job);p->file=info;
    p->status.width=source->width;p->status.height=source->height;p->status.compact=!!source->compact;
    p->live_only=!!source->live_only;p->status.following=!!source->follow;p->status.flat=0;
    p->status.yaw=-.45f;p->status.tilt=.75f;p->status.zoom=1;discard(p);
    pthread_mutex_unlock(&p->mutex);return 0;
}
static void preview_clear(GcodePreview *p){
    if(!p)return;
    pthread_mutex_lock(&p->mutex);p->path[0]=0;p->job_filename[0]=0;discard(p);pthread_mutex_unlock(&p->mutex);
}
static void preview_pause(GcodePreview *p,int paused){
    if(!p)return;
    pthread_mutex_lock(&p->mutex);
    if(p->status.paused!=!!paused){p->status.paused=!!paused;request(p);}
    pthread_mutex_unlock(&p->mutex);
}
static int preview_viewport(GcodePreview *p,uint32_t width,uint32_t height,int compact,int live_only){
    if(!p||!valid_view(width,height,compact))return -1;
    pthread_mutex_lock(&p->mutex);
    viewport(p,width,height,compact,live_only);
    pthread_mutex_unlock(&p->mutex);return 0;
}
static void preview_action(GcodePreview *p,int action,float x,float y){
    if(!p||!isfinite(x)||!isfinite(y))return;
    pthread_mutex_lock(&p->mutex);
    switch(action){
    case GCODE_PREVIEW_RESET:
        p->status.yaw=-.45f;p->status.tilt=.75f;p->status.zoom=1;p->status.flat=0;
        if(p->status.live_available)p->status.following=1;
        break;
    case GCODE_PREVIEW_ROTATE:
        if(!p->status.flat){p->status.yaw=remainderf(p->status.yaw+x,2*(float)M_PI);
            p->status.tilt=fmaxf(-1.4f,fminf(1.4f,p->status.tilt+y));}
        break;
    case GCODE_PREVIEW_ZOOM:
        if(x>0)p->status.zoom=fmaxf(.3f,fminf(6,p->status.zoom*x));
        break;
    case GCODE_PREVIEW_FLAT:
        p->status.flat=x!=0;p->status.yaw=p->status.flat?0:-.45f;p->status.tilt=p->status.flat?0:.75f;break;
    case GCODE_PREVIEW_LAYER:
        p->status.following=0;p->status.live_follow=0;
        if(p->status.layers)p->status.layer=(unsigned)fmaxf(0,fminf((float)(p->status.layers-1),x));
        break;
    case GCODE_PREVIEW_FOLLOW:p->status.following=1;break;
    case GCODE_PREVIEW_REFRESH:break;
    default:pthread_mutex_unlock(&p->mutex);return;
    }
    request(p);pthread_mutex_unlock(&p->mutex);
}
static int preview_poll(GcodePreview *p,GcodePreviewStatus *status,uint8_t *pixels,size_t capacity,size_t stride){
    if(!p||!status)return -2;
    if(pthread_mutex_trylock(&p->mutex))return -1;
    size_t row=(size_t)p->status.width*4,height=p->status.height;
    if(!stride)stride=row;
    if(pixels&&(stride<row||capacity<row||(height>1&&stride>(capacity-row)/(height-1)))){
        pthread_mutex_unlock(&p->mutex);return -2;
    }
    *status=p->status;
    int result=0;
    if(pixels&&p->status.visible){
        const RenderView *view=&p->frame_view;
        status->layer=view->layer;status->layer_height=p->frame_height;
        status->flat=view->flat;status->yaw=view->yaw;status->tilt=view->tilt;status->zoom=view->zoom;
        status->live_follow=view->completed!=SIZE_MAX;
        if(p->frame_ready){
            for(size_t y=0;y<height;y++)memcpy(pixels+y*stride,p->pixels+y*row,row);
            p->frame_ready=0;result=1;
        }
    }
    pthread_mutex_unlock(&p->mutex);return result;
}
__attribute__((visibility("default"))) const GcodePreviewApi *gcode_preview_get_api(uint32_t abi){
    static const GcodePreviewApi api={GCODE_PREVIEW_ABI,sizeof(GcodePreviewApi),preview_create,preview_destroy,
        preview_load,preview_clear,preview_pause,preview_viewport,preview_action,preview_poll};
    return abi==GCODE_PREVIEW_ABI?&api:NULL;
}
