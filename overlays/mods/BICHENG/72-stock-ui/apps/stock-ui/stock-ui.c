#define _GNU_SOURCE
#include "gpu.h"
#include "stock-abi.h"
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
void stock_log(const char *format,...) {
    FILE *f=fopen("/tmp/stock-ui.log","a");if(!f)return;
    if(ftell(f)>65536){fclose(f);f=fopen("/tmp/stock-ui.log","w");if(!f)return;}
    va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);fclose(f);
}

/* These entry points belong only to the SHA-256 checked vendor 2.0.0.205 GUI. */
static uintptr_t base;
#define FN(off,ret,...) ((ret(*)(__VA_ARGS__))(base+(off)))
typedef struct {void *target,*original;uint32_t code;void *user,*param,*prev;uint8_t flags;} Event;
typedef struct {uint8_t magic,cf;uint16_t flags,w,h,stride,reserved;uint32_t size;const uint8_t *data;void *r1,*r2;} Image;
static int (*real_usleep)(useconds_t);
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static atomic_uint generation;
static int worker_started,wanted,active,frame_ready,flat,close_pending;
static int worker_idle=1,guard_armed;
static uint64_t started;
static unsigned layers,layer;
static float layer_height,yaw=-0.45f,tilt=0.75f,zoom=1;
static char filename[4096],message[128];
static char previous_status[128];
static uint8_t frame[WIDTH*HEIGHT*4],display_pixels[WIDTH*HEIGHT*4];
static void *badge,*source_image,*preview,*screen,*status_label,*mode_label;
static uintptr_t font;
static Image descriptor={.magic=0x19,.cf=0x10,.w=WIDTH,.h=HEIGHT,.stride=WIDTH*4,.size=sizeof(display_pixels),.data=display_pixels};
static uint64_t milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ull+t.tv_nsec/1000000;}
static int model_path(const char *src,char *out,size_t size);
static void arm_guard(void){
    int fd=open("/tmp/stock-ui.guard",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(fd>=0){dprintf(fd,"%d\n",getpid());close(fd);guard_armed=1;}
}
static void style(void *o,int prop,uintptr_t value){FN(lv_obj_set_local_style_prop,void,void*,int,uintptr_t,uint32_t)(o,prop,value,0);}
static void pos(void *o,int x,int y){FN(lv_obj_set_pos,void,void*,int,int)(o,x,y);}
static void size(void *o,int w,int h){FN(lv_obj_set_size,void,void*,int,int)(o,w,h);}
static void text(void *o,const char *s){FN(lv_label_set_text,void,void*,const char*)(o,s);}
static void *create(uintptr_t cls,void *parent){
    void *o=FN(lv_obj_class_create_obj,void*,const void*,void*)((void*)(base+cls),parent);
    if(o)FN(lv_obj_class_init_obj,void,void*)(o);
    return o;
}
static void add_event(void *o,void (*cb)(Event*),uint32_t code,void *user){
    FN(lv_obj_add_event_cb,void*,void*,void(*)(Event*),uint32_t,void*)(o,cb,code,user);
}
static void *label(void *parent,const char *s,int x,int y){
    void *o=create(stock_label_class,parent);if(!o)return NULL;
    if(font)style(o,90,font);
    style(o,88,0xffffff);text(o,s);pos(o,x,y);return o;
}
static void request_frame(void){wanted=1;pthread_cond_signal(&cond);}
static void *worker(void *unused) {
    (void)unused;
    Model m={0};Gpu *g=NULL;unsigned loaded=0;
    uint8_t *pixels=malloc(sizeof(frame));
    if(!pixels){
        pthread_mutex_lock(&mutex);snprintf(message,sizeof(message),"内存不足");
        worker_started=0;worker_idle=1;pthread_mutex_unlock(&mutex);return NULL;
    }
    for(;;) {
        pthread_mutex_lock(&mutex);
        while(!wanted)pthread_cond_wait(&cond,&mutex);
        wanted=0;int run=active;unsigned seq=atomic_load(&generation);
        unsigned selected=layer;int top=flat;float a=yaw,b=tilt,z=zoom;
        char path[sizeof(filename)];snprintf(path,sizeof(path),"%s",filename);
        pthread_mutex_unlock(&mutex);
        if(!run){
            gpu_destroy(g);g=NULL;gcode_free(&m);loaded=0;
            pthread_mutex_lock(&mutex);worker_idle=1;pthread_mutex_unlock(&mutex);
            continue;
        }
        if(loaded!=seq){
            gpu_destroy(g);g=NULL;
            int ok=gcode_load(&m,path,&generation,seq);
            if(atomic_load(&generation)!=seq)continue;
            if(!ok)g=gpu_create(&m,m.error,sizeof(m.error));
            pthread_mutex_lock(&mutex);
            if(atomic_load(&generation)==seq){
                layers=m.layers;layer=m.layers?m.layers-1:0;selected=layer;
                snprintf(message,sizeof(message),"%s",g?"":m.error);
            }
            pthread_mutex_unlock(&mutex);loaded=seq;
        }
        if(g&&atomic_load(&generation)==seq) {
            uint64_t start=milliseconds();
            int rc=gpu_render(g,&m,selected,top,a,b,z,pixels);
            pthread_mutex_lock(&mutex);
            if(active&&atomic_load(&generation)==seq){
                if(rc)snprintf(message,sizeof(message),"GPU 绘制失败");
                else {memcpy(frame,pixels,sizeof(frame));frame_ready=1;layer_height=m.layer_z[selected<m.layers?selected:m.layers-1];}
            }
            pthread_mutex_unlock(&mutex);
            static unsigned rendered;rendered++;
            if(rendered==1||rendered%100==0)stock_log("frame=%u render_ms=%llu\n",rendered,(unsigned long long)(milliseconds()-start));
        }
    }
    return NULL;
}
static void preview_deleted(Event *e){
    if(e->code==38&&e->target==preview){preview=NULL;status_label=NULL;mode_label=NULL;close_pending=1;}
}
static void control(Event *e) {
    if(e->code!=7)return;
    intptr_t action=(intptr_t)e->user;
    if(action==0){close_pending=1;return;}
    pthread_mutex_lock(&mutex);
    if(action==1){flat=!flat;yaw=flat?0:-0.45f;tilt=flat?0:0.75f;}
    if(action==2)zoom=fmaxf(0.3f,zoom/1.25f);
    if(action==3)zoom=fminf(6.0f,zoom*1.25f);
    if(action==4&&layer>0)layer--;
    if(action==5&&layer+1<layers)layer++;
    request_frame();pthread_mutex_unlock(&mutex);
}
static void *button(void *parent,const char *s,int x,int y,int w,int h,int action){
    void *o=create(stock_button_class,parent);if(!o)return NULL;
    pos(o,x,y);size(o,w,h);
    for(int prop=16;prop<=19;prop++)style(o,prop,0);
    style(o,12,10);style(o,28,0x292d33);style(o,29,255);style(o,48,0);style(o,60,0);
    void *l=label(o,s,0,(h-20)/2);if(l){size(l,w,22);style(l,94,2);}
    if(action<6)add_event(o,control,7,(void*)(intptr_t)action);
    return o;
}
static void touch(Event *e){
    static int last_x,last_y,drag;
    if(e->code!=1&&e->code!=2&&e->code!=8&&e->code!=3)return;
    if(e->code==8||e->code==3){drag=0;return;}
    int32_t point[2];void *indev=FN(lv_event_get_indev,void*,Event*)(e);
    if(!indev)return;
    FN(lv_indev_get_point,void,void*,int32_t*)(indev,point);
    if(e->code==1){last_x=point[0];last_y=point[1];drag=1;}
    if(!drag)return;
    pthread_mutex_lock(&mutex);
    if(point[1]>=260&&layers){
        int x=point[0]-60;if(x<0)x=0;if(x>360)x=360;
        layer=(unsigned)((uint64_t)x*(layers-1)/360);request_frame();
    } else if(e->code==2&&!flat&&point[1]>48) {
        yaw+=(point[0]-last_x)*0.012f;
        tilt=fmaxf(-1.4f,fminf(1.4f,tilt+(point[1]-last_y)*0.012f));request_frame();
    }
    pthread_mutex_unlock(&mutex);last_x=point[0];last_y=point[1];
}
static void open_preview(Event *e){
    if(e->code!=7||preview)return;
    char selected[sizeof(filename)];
    if(!source_image||!model_path(FN(lv_image_get_src,const char*,void*)(source_image),selected,sizeof(selected)))return;
    arm_guard();
    screen=FN(lv_display_get_screen_active,void*,void*)(NULL);
    preview=create(stock_image_class,screen);if(!preview)return;
    memset(display_pixels,20,sizeof(display_pixels));
    for(size_t i=3;i<sizeof(display_pixels);i+=4)display_pixels[i]=255;
    FN(lv_image_set_src,void,void*,const void*)(preview,&descriptor);pos(preview,0,0);
    FN(lv_obj_add_flag,void,void*,uint32_t)(preview,2);
    FN(lv_obj_remove_flag,void,void*,uint32_t)(preview,16);
    add_event(preview,touch,0,NULL);add_event(preview,preview_deleted,38,NULL);
    if(!button(preview,"返回",8,6,68,36,0)){close_pending=1;return;}
    label(preview,"G-code",90,15);
    void *mode=button(preview,"3D",280,6,64,36,1);
    if(!mode){close_pending=1;return;}
    mode_label=FN(lv_obj_get_child,void*,void*,int)(mode,0);
    button(preview,"−",352,6,54,36,2);button(preview,"+",414,6,58,36,3);
    button(preview,"−",8,277,44,36,4);button(preview,"+",428,277,44,36,5);
    status_label=label(preview,"正在读取路径…",66,285);
    if(!status_label||!mode_label){close_pending=1;return;}
    previous_status[0]=0;
    pthread_mutex_lock(&mutex);
    snprintf(filename,sizeof(filename),"%s",selected);
    active=1;worker_idle=0;flat=0;layer=0;layers=0;frame_ready=0;yaw=-0.45f;tilt=0.75f;zoom=1;message[0]=0;
    atomic_fetch_add(&generation,1);request_frame();
    if(!worker_started){
        pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,512*1024);
        pthread_t thread;int rc=pthread_create(&thread,&attr,worker,NULL);pthread_attr_destroy(&attr);
        if(!rc){pthread_detach(thread);worker_started=1;}else snprintf(message,sizeof(message),"无法启动预览");
    }
    pthread_mutex_unlock(&mutex);
    void *indev=FN(lv_event_get_indev,void*,Event*)(e);
    if(indev){void *t=FN(lv_indev_get_read_timer,void*,void*)(indev);if(t)FN(lv_timer_set_period,void,void*,unsigned)(t,22);}
}
static void badge_deleted(Event *e){if(e->code==38&&e->target==badge){badge=NULL;source_image=NULL;}}
static uintptr_t find_font(void *o,unsigned depth){
    if(!o||depth>14||!FN(lv_obj_is_visible,int,void*)(o))return 0;
    if(*(uintptr_t*)o-base==stock_label_class){
        const unsigned char *s=FN(lv_label_get_text,const unsigned char*,void*)(o);
        for(;s&&*s;s++)if(*s>=128)return FN(lv_obj_get_style_prop,uintptr_t,void*,uint32_t,int)(o,0,90);
    }
    unsigned n=FN(lv_obj_get_child_count,unsigned,void*)(o);
    for(unsigned i=0;i<n&&i<256;i++){
        uintptr_t f=find_font(FN(lv_obj_get_child,void*,void*,int)(o,i),depth+1);if(f)return f;
    }
    return 0;
}
static int model_path(const char *src,char *out,size_t size){
    if(!src||strncmp(src,"A:/userdata/gcodes/",19))return 0;
    const char *thumb=strstr(src,"/.thumbs/");if(!thumb)return 0;
    const char *name=thumb+9,*suffix=strrchr(name,'-');if(!suffix)return 0;
    unsigned w,h;int used=0;
    if(sscanf(suffix,"-%ux%u.png%n",&w,&h,&used)!=2||suffix[used]||w<112||h<112)return 0;
    int n=snprintf(out,size,"%.*s/%.*s.gcode",(int)(thumb-(src+2)),src+2,(int)(suffix-name),name);
    if(n<0||(size_t)n>=size)return 0;
    char real[4096];if(!realpath(out,real)||strncmp(real,"/userdata/gcodes/",17))return 0;
    snprintf(out,size,"%s",real);return access(out,R_OK)==0;
}
static void scan(void *o,unsigned depth){
    if(!o||depth>14)return;
    if(!FN(lv_obj_is_visible,int,void*)(o))return;
    uintptr_t cls=*(uintptr_t*)o-base;
    if(cls==stock_image_class&&o!=preview&&!badge) {
        const unsigned char *src=FN(lv_image_get_src,const unsigned char*,void*)(o);
        char candidate[sizeof(filename)];
        int32_t a[4];FN(lv_obj_get_coords,void,void*,int32_t*)(o,a);
        if(src&&*src=='A'&&a[2]-a[0]>=100&&a[3]-a[1]>=100&&model_path((const char*)src,candidate,sizeof(candidate))){
            font=find_font(FN(lv_display_get_screen_active,void*,void*)(NULL),0);
            source_image=o;badge=button(o,"路径预览",0,a[3]-a[1]-27,a[2]-a[0]+1,28,6);
            if(badge){add_event(badge,open_preview,7,NULL);add_event(badge,badge_deleted,38,NULL);}
            return;
        }
    }
    unsigned n=FN(lv_obj_get_child_count,unsigned,void*)(o);
    for(unsigned i=0;i<n&&i<256;i++)scan(FN(lv_obj_get_child,void*,void*,int)(o,i),depth+1);
}
static void pump(void){
    static uint64_t last,scanned;static int refresh_set;
    uint64_t now=milliseconds();if(now-last<22)return;last=now;
    FN(lv_lock,void,void)();
    void *current=FN(lv_display_get_screen_active,void*,void*)(NULL);
    if(current&&!refresh_set){
        void *global=*(void**)(base+stock_global_got),*disp=*(void**)((char*)global+0x28);
        void *timer=*(void**)((char*)disp+0x380);
        stock_log("refresh period %u -> 22 ms\n",*(unsigned*)timer);
        FN(lv_timer_set_period,void,void*,unsigned)(timer,22);refresh_set=1;
    }
    if(preview&&current!=screen)close_pending=1;
    if(close_pending){
        close_pending=0;
        pthread_mutex_lock(&mutex);active=0;atomic_fetch_add(&generation,1);request_frame();pthread_mutex_unlock(&mutex);
        void *old=preview;preview=NULL;status_label=NULL;mode_label=NULL;
        if(old)FN(lv_obj_delete,void,void*)(old);
    }
    if(preview&&pthread_mutex_trylock(&mutex)==0){
        if(frame_ready){memcpy(display_pixels,frame,sizeof(frame));frame_ready=0;FN(lv_obj_invalidate,void,void*)(preview);}
        char s[128];
        if(message[0])snprintf(s,sizeof(s),"%s",message);
        else if(layers)snprintf(s,sizeof(s),"层 %u / %u · %.2f mm",layer+1,layers,layer_height);
        else snprintf(s,sizeof(s),"正在读取路径…");
        if(strcmp(s,previous_status)){text(status_label,s);snprintf(previous_status,sizeof(previous_status),"%s",s);}
        static int was_flat=-1;if(flat!=was_flat){text(mode_label,flat?"2D":"3D");was_flat=flat;}
        pthread_mutex_unlock(&mutex);
    }
    if(!preview&&now-scanned>=500){
        scanned=now;
        if(badge&&(!source_image||!FN(lv_obj_is_visible,int,void*)(source_image))){
            void *old=badge;badge=NULL;source_image=NULL;FN(lv_obj_delete,void,void*)(old);
        }
        if(current)scan(current,0);
    }
    if(guard_armed&&now-started>10000&&pthread_mutex_trylock(&mutex)==0){
        if(!active&&worker_idle){unlink("/tmp/stock-ui.guard");guard_armed=0;}
        pthread_mutex_unlock(&mutex);
    }
    FN(lv_unlock,void,void)();
}
static int locate(struct dl_phdr_info *i,size_t n,void *v){
    (void)n;(void)v;if(!i->dlpi_name[0]){base=i->dlpi_addr;return 1;}return 0;
}
__attribute__((constructor)) static void init(void){
    real_usleep=dlsym(RTLD_NEXT,"usleep");
    char path[128]={0};if(readlink("/proc/self/exe",path,sizeof(path)-1)<0||strcmp(path,"/usr/bin/gui"))return;
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);struct stat st;
    if(fd<0)return;
    if(fstat(fd,&st)||st.st_size!=7010568){close(fd);return;}
    void *data=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(data==MAP_FAILED)return;
    unsigned char digest[32];char hex[65];
    unsigned char *(*sha)(const void*,size_t,unsigned char*)=dlsym(RTLD_DEFAULT,"SHA256");
    if(!sha){munmap(data,st.st_size);return;}sha(data,st.st_size,digest);munmap(data,st.st_size);
    for(int j=0;j<32;j++)sprintf(hex+j*2,"%02x",digest[j]);
    if(strcmp(hex,"bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4"))return;
    if(access("/tmp/stock-ui.guard",F_OK)==0){stock_log("previous extension did not exit cleanly; keeping factory UI\n");return;}
    started=milliseconds();arm_guard();
    dl_iterate_phdr(locate,NULL);
    stock_log("supported original GUI verified, base=%lx\n",base);
}
int usleep(useconds_t usec){
    if(!real_usleep)real_usleep=dlsym(RTLD_NEXT,"usleep");
    uintptr_t ra=(uintptr_t)__builtin_return_address(0)-base;
    if(base&&(ra==stock_main_sleep||ra==stock_main_short_sleep)){
        pump();useconds_t cap=preview?22000:500000;if(usec>cap)usec=cap;
    }
    return real_usleep(usec);
}
