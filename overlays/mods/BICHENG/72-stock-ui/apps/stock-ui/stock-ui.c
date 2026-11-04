#define _GNU_SOURCE
#include "gcode-preview.h"
#define WIDTH 480
#define HEIGHT 320
#include "stock-abi.h"
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>
void stock_log(const char *format,...) {
    FILE *f=fopen("/tmp/stock-ui.log","a");if(!f)return;
    struct stat st;
    if(!fstat(fileno(f),&st)&&st.st_size>65536){fclose(f);f=fopen("/tmp/stock-ui.log","w");if(!f)return;}
    va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);fclose(f);
}

/* Private entry points are selected only after the exact vendor GUI hash matches. */
static uintptr_t base;
static const StockAbi *abi;
#define FN(off,ret,...) ((ret(*)(__VA_ARGS__))(base+abi->off))
typedef struct {void *target,*original;uint32_t code;void *user,*param,*prev;uint8_t flags;} Event;
typedef struct {uint8_t magic,cf;uint16_t flags,w,h,stride,reserved;uint32_t size;const uint8_t *data;void *r1,*r2;} Image;
static const GcodePreviewApi *engine;
static GcodePreview *session;
static GcodePreviewStatus status;
static int active,close_pending,guard_armed,inline_show;
static int thumb_width,thumb_height;
static uint64_t started;
static char thumbnail[4096],message[128];
static char previous_status[128];
static uint8_t display_pixels[WIDTH*HEIGHT*4],inline_pixels[WIDTH*HEIGHT*4];
static void *entry,*source_image,*preview,*screen,*status_label,*eta_label,*mode_label,*follow_button,*inline_image;
static uintptr_t font;
static Image descriptor={.magic=0x19,.cf=0x10,.w=WIDTH,.h=HEIGHT,.stride=WIDTH*4,.size=sizeof(display_pixels),.data=display_pixels};
static Image inline_descriptor={.magic=0x19,.cf=0x10,.data=inline_pixels};
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
    void *o=create(abi->stock_label_class,parent);if(!o)return NULL;
    if(font)style(o,90,font);
    style(o,88,0xffffff);text(o,s);pos(o,x,y);return o;
}
static int load_source(const char *path,int width,int height,int compact,int live_only){
    if(!engine)engine=gcode_preview_get_api(GCODE_PREVIEW_ABI);
    if(engine&&!session)session=engine->create();
    if(!session){snprintf(message,sizeof(message),"无法启动预览");return -1;}
    arm_guard();
    GcodePreviewSource source={path,NULL,(uint32_t)width,(uint32_t)height,compact,live_only,1};
    status=(GcodePreviewStatus){.phase=GCODE_PREVIEW_LOADING,.offline=1};
    if(engine->load(session,&source)){engine->clear(session);snprintf(message,sizeof(message),"无法读取 G-code 文件");return -1;}
    message[0]=0;return 0;
}
static void preview_deleted(Event *e){
    if(e->code==38&&e->target==preview){preview=NULL;status_label=NULL;eta_label=NULL;mode_label=NULL;follow_button=NULL;close_pending=1;}
}
static void control(Event *e) {
    if(e->code!=7)return;
    intptr_t action=(intptr_t)e->user;
    if(action==0){close_pending=1;return;}
    if(!session)return;
    if(action==1)engine->action(session,GCODE_PREVIEW_FLAT,!status.flat,0);
    if(action==2)engine->action(session,GCODE_PREVIEW_ZOOM,1.0f/1.25f,0);
    if(action==3)engine->action(session,GCODE_PREVIEW_ZOOM,1.25f,0);
    if(action==6)engine->action(session,GCODE_PREVIEW_FOLLOW,0,0);
}
static void *button(void *parent,const char *s,int x,int y,int w,int h,int action){
    void *o=create(abi->stock_button_class,parent);if(!o)return NULL;
    FN(lv_obj_add_flag,void,void*,uint32_t)(o,2|(1u<<13));
    FN(lv_obj_remove_flag,void,void*,uint32_t)(o,(1u<<2)|(1u<<4)|(1u<<8)|(1u<<9)|(1u<<15));
    pos(o,x,y);size(o,w,h);
    for(int prop=16;prop<=19;prop++)style(o,prop,0);
    style(o,12,10);style(o,28,0x292d33);style(o,29,255);style(o,48,0);style(o,60,0);
    if(s){void *l=label(o,s,0,(h-20)/2);if(l){size(l,w,22);style(l,94,2);}}
    if(action<=6)add_event(o,control,7,(void*)(intptr_t)action);
    return o;
}
static void touch(Event *e){
    static int last_x,last_y,drag;
    if(e->code!=1&&e->code!=2&&e->code!=8&&e->code!=3)return;
    if(e->code==8||e->code==3){drag=0;return;}
    int32_t point[2];void *indev=FN(lv_event_get_indev,void*,Event*)(e);
    if(!indev)return;
    FN(lv_indev_get_point,void,void*,int32_t*)(indev,point);
    if(e->code==1){
        last_x=point[0];last_y=point[1];
        drag=point[1]>=300?2:1;
    }
    if(!drag)return;
    if(session&&drag==2&&status.layers){
        int x=point[0]-12;if(x<0)x=0;if(x>456)x=456;
        unsigned selected=(unsigned)(((uint64_t)x*(status.layers-1)+228)/456);
        if(selected!=status.layer)engine->action(session,GCODE_PREVIEW_LAYER,(float)selected,0);
    }else if(session&&e->code==2&&!status.flat){
        engine->action(session,GCODE_PREVIEW_ROTATE,(point[0]-last_x)*0.012f,(point[1]-last_y)*0.012f);
    }
    last_x=point[0];last_y=point[1];
}
static void open_preview(Event *e){
    if(e->code!=7||preview)return;
    char selected[4096];
    if(!source_image||!model_path(FN(lv_image_get_src,const char*,void*)(source_image),selected,sizeof(selected)))return;
    arm_guard();
    screen=FN(lv_display_get_screen_active,void*,void*)(NULL);
    void *top=FN(lv_display_get_layer_top,void*,void*)(NULL);
    if(!top)return;
    preview=create(abi->stock_image_class,top);if(!preview)return;
    memset(display_pixels,20,sizeof(display_pixels));
    for(size_t i=3;i<sizeof(display_pixels);i+=4)display_pixels[i]=255;
    FN(lv_image_set_src,void,void*,const void*)(preview,&descriptor);pos(preview,0,0);
    FN(lv_obj_add_flag,void,void*,uint32_t)(preview,2|(1u<<13)|(1u<<18));
    FN(lv_obj_remove_flag,void,void*,uint32_t)(preview,(1u<<4)|(1u<<8)|(1u<<9)|(1u<<15));
    add_event(preview,touch,0,NULL);add_event(preview,preview_deleted,38,NULL);
    if(!button(preview,"返回",8,8,60,34,0)){close_pending=1;return;}
    follow_button=button(preview,"当前层",386,266,86,34,6);
    if(!follow_button){close_pending=1;return;}
    void *mode=button(preview,"3D",414,8,58,34,1);
    if(!mode){close_pending=1;return;}
    mode_label=FN(lv_obj_get_child,void*,void*,int)(mode,0);
    button(preview,"−",326,8,38,34,2);button(preview,"+",370,8,38,34,3);
    status_label=label(preview,"正在读取路径…",12,258);
    eta_label=label(preview,"",12,280);
    if(!status_label||!eta_label||!mode_label){close_pending=1;return;}
    previous_status[0]=0;
    active=1;inline_show=0;
    load_source(selected,WIDTH,HEIGHT,0,0);
}
static void clear_thumbnail(void){
    thumbnail[0]=0;inline_show=0;
    if(!active&&session){engine->clear(session);status.phase=GCODE_PREVIEW_EMPTY;status.visible=0;}
}

static void entry_deleted(Event *e){
    if(e->code==38&&e->target==entry){entry=NULL;source_image=NULL;inline_image=NULL;clear_thumbnail();}
}
static void entry_touch(Event *e){
    static int x,y,moved,pressed;
    if(e->code==3){pressed=0;return;}
    if(e->code!=1&&e->code!=2&&e->code!=7)return;
    void *indev=FN(lv_event_get_indev,void*,Event*)(e);if(!indev)return;
    int32_t point[2];FN(lv_indev_get_point,void,void*,int32_t*)(indev,point);
    if(e->code==1){x=point[0];y=point[1];moved=0;pressed=1;}
    if(abs(point[0]-x)>10||abs(point[1]-y)>10)moved=1;
    if(e->code==7){if(pressed&&!moved)open_preview(e);pressed=0;}
}
static uintptr_t find_font(void *o,unsigned depth){
    if(!o||depth>14||!FN(lv_obj_is_visible,int,void*)(o))return 0;
    if(*(uintptr_t*)o-base==abi->stock_label_class){
        const unsigned char *s=FN(lv_label_get_text,const unsigned char*,void*)(o);
        for(;s&&*s;s++)if(*s>=128)return FN(lv_obj_get_style_prop,uintptr_t,void*,uint32_t,int)(o,0,90);
    }
    unsigned n=FN(lv_obj_get_child_count,unsigned,void*)(o);
    for(unsigned i=0;i<n&&i<256;i++){
        uintptr_t f=find_font(FN(lv_obj_get_child,void*,void*,int)(o,i),depth+1);if(f)return f;
    }
    return 0;
}
static const char *machine_time(void *o,unsigned depth){
    if(!o||depth>14||!FN(lv_obj_is_visible,int,void*)(o))return NULL;
    if(*(uintptr_t*)o-base==abi->stock_label_class){
        const char *s=FN(lv_label_get_text,const char*,void*)(o);
        unsigned h,m;int used=0;
        if(s&&(!strncmp(s,"剩余",strlen("剩余"))||
               (sscanf(s,"%uh%um%n",&h,&m,&used)==2&&used>0&&!s[used])))return s;
    }
    unsigned n=FN(lv_obj_get_child_count,unsigned,void*)(o);
    for(unsigned i=0;i<n&&i<256;i++){
        const char *s=machine_time(FN(lv_obj_get_child,void*,void*,int)(o,i),depth+1);if(s)return s;
    }
    return NULL;
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
static void update_thumbnail(const char *path,int w,int h){
    if(w<1||w>WIDTH||h<1||h>HEIGHT){clear_thumbnail();return;}
    if(strcmp(path,thumbnail)||w!=thumb_width||h!=thumb_height){
        snprintf(thumbnail,sizeof(thumbnail),"%s",path);thumb_width=w;thumb_height=h;inline_show=0;
        if(!active)load_source(path,w,h,1,1);
    }
}
static void lock_page_scroll(void *image){
    for(void *o=image;o;o=*(void**)((char*)o+abi->object_parent_offset)){
        int32_t a[4];FN(lv_obj_get_coords,void,void*,int32_t*)(o,a);
        if(o==image||(a[0]==0&&a[1]==0&&a[2]==WIDTH-1&&a[3]==HEIGHT-1))
            FN(lv_obj_remove_flag,void,void*,uint32_t)(o,(1u<<4)|(1u<<8)|(1u<<9)|(1u<<15));
    }
}
static void scan(void *o,unsigned depth){
    if(!o||depth>14)return;
    if(!FN(lv_obj_is_visible,int,void*)(o))return;
    uintptr_t cls=*(uintptr_t*)o-base;
    if(cls==abi->stock_image_class&&o!=preview&&!entry) {
        const unsigned char *src=FN(lv_image_get_src,const unsigned char*,void*)(o);
        char candidate[sizeof(filename)];
        int32_t a[4];FN(lv_obj_get_coords,void,void*,int32_t*)(o,a);
        int w=a[2]-a[0]+1,h=a[3]-a[1]+1;
        if(src&&*src=='A'&&w>=60&&h>=60&&w<=WIDTH&&h<=HEIGHT&&model_path((const char*)src,candidate,sizeof(candidate))){
            font=find_font(FN(lv_display_get_screen_active,void*,void*)(NULL),0);
            source_image=o;
            lock_page_scroll(o);
            inline_image=create(abi->stock_image_class,o);
            if(inline_image){
                pos(inline_image,0,0);FN(lv_obj_add_flag,void,void*,uint32_t)(inline_image,1);
            }
            entry=button(o,NULL,0,0,w,h,7);
            if(!entry){if(inline_image)FN(lv_obj_delete,void,void*)(inline_image);inline_image=NULL;source_image=NULL;return;}
            style(entry,29,0);
            FN(lv_obj_set_local_style_prop,void,void*,int,uintptr_t,uint32_t)(entry,29,0,0x20);
            add_event(entry,entry_touch,0,NULL);add_event(entry,entry_deleted,38,NULL);
            update_thumbnail(candidate,w,h);
            return;
        }
    }
    unsigned n=FN(lv_obj_get_child_count,unsigned,void*)(o);
    for(unsigned i=0;i<n&&i<256;i++)scan(FN(lv_obj_get_child,void*,void*,int)(o,i),depth+1);
}
static void pump(void){
    static uint64_t last,scanned,time_scanned;
    uint64_t now=milliseconds();if(now-last<22)return;last=now;
    FN(lv_lock,void,void)();
    void *current=FN(lv_display_get_screen_active,void*,void*)(NULL);
    if(preview&&current!=screen)close_pending=1;
    if(close_pending){
        close_pending=0;
        active=0;inline_show=0;
        if(thumbnail[0])load_source(thumbnail,thumb_width,thumb_height,1,1);
        else if(session){engine->clear(session);status.phase=GCODE_PREVIEW_EMPTY;status.visible=0;}
        void *old=preview;preview=NULL;status_label=NULL;eta_label=NULL;mode_label=NULL;follow_button=NULL;
        if(old)FN(lv_obj_delete,void,void*)(old);
    }
    int new_frame=session?engine->poll(session,&status,active?display_pixels:inline_pixels,sizeof(display_pixels),0):0;
    if(new_frame<0)new_frame=0;
    if(preview){
        if(new_frame)FN(lv_obj_invalidate,void,void*)(preview);
        char s[128];
        if(message[0]||status.message[0])snprintf(s,sizeof(s),"%s",message[0]?message:status.message);
        else if(status.layers)snprintf(s,sizeof(s),"层 %u / %u · %.2f mm",status.layer+1,status.layers,status.layer_height);
        else snprintf(s,sizeof(s),"正在读取路径…");
        if(strcmp(s,previous_status)){text(status_label,s);snprintf(previous_status,sizeof(previous_status),"%s",s);}
        const char *mode=status.flat?"2D":"3D";
        if(strcmp(FN(lv_label_get_text,const char*,void*)(mode_label),mode))text(mode_label,mode);
        uintptr_t color=status.live_follow?0x2165d7:0x292d33;
        if((FN(lv_obj_get_style_prop,uintptr_t,void*,uint32_t,int)(follow_button,0,28)&0xffffff)!=color)style(follow_button,28,color);
        void *follow_label=FN(lv_obj_get_child,void*,void*,int)(follow_button,0);
        color=status.live_available?0xffffff:0x89929b;
        if(follow_label&&(FN(lv_obj_get_style_prop,uintptr_t,void*,uint32_t,int)(follow_label,0,88)&0xffffff)!=color)style(follow_label,88,color);
    }
    if(preview&&now-time_scanned>=500){
        time_scanned=now;
        const char *native=machine_time(current,0);char value[80];
        if(status.offline)snprintf(value,sizeof(value),"打印状态暂不可用");
        else snprintf(value,sizeof(value),"%s%s",native&&strncmp(native,"剩余",strlen("剩余"))?"预计 ":"",native?native:"");
        if(strcmp(FN(lv_label_get_text,const char*,void*)(eta_label),value))text(eta_label,value);
    }
    inline_show=!active&&status.visible;
    if(inline_image){
        if(inline_show&&new_frame){
            inline_descriptor.w=status.width;inline_descriptor.h=status.height;
            inline_descriptor.stride=status.width*4;inline_descriptor.size=status.width*status.height*4;
            FN(lv_image_set_src,void,void*,const void*)(inline_image,&inline_descriptor);
            FN(lv_obj_invalidate,void,void*)(inline_image);
        }
        if(inline_show)FN(lv_obj_remove_flag,void,void*,uint32_t)(inline_image,1);
        else FN(lv_obj_add_flag,void,void*,uint32_t)(inline_image,1);
    }
    if(!preview&&now-scanned>=500){
        scanned=now;
        if(entry&&(!source_image||!FN(lv_obj_is_visible,int,void*)(source_image))){
            void *old=entry,*old_image=inline_image;
            entry=NULL;source_image=NULL;inline_image=NULL;clear_thumbnail();
            if(old_image)FN(lv_obj_delete,void,void*)(old_image);
            FN(lv_obj_delete,void,void*)(old);
        }
        if(entry&&source_image){
            char candidate[4096];int32_t a[4];
            FN(lv_obj_get_coords,void,void*,int32_t*)(source_image,a);
            if(model_path(FN(lv_image_get_src,const char*,void*)(source_image),candidate,sizeof(candidate)))
                update_thumbnail(candidate,a[2]-a[0]+1,a[3]-a[1]+1);
            else clear_thumbnail();
        }
        if(current)scan(current,0);
    }
    if(guard_armed&&now-started>10000&&!active&&!inline_show&&!thumbnail[0]){
        if(session){engine->destroy(session);session=NULL;}
        unlink("/tmp/stock-ui.guard");guard_armed=0;
    }
    FN(lv_unlock,void,void)();
}
static int locate(struct dl_phdr_info *i,size_t n,void *v){
    (void)n;(void)v;if(!i->dlpi_name[0]){base=i->dlpi_addr;return 1;}return 0;
}
__attribute__((constructor)) static void init(void){
    char path[128]={0};if(readlink("/proc/self/exe",path,sizeof(path)-1)<0||strcmp(path,"/usr/bin/gui"))return;
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);struct stat st;
    if(fd<0)return;
    if(fstat(fd,&st)){close(fd);return;}
    int supported=0;
    for(size_t i=0;i<sizeof(stock_abis)/sizeof(stock_abis[0]);i++)
        if((size_t)st.st_size==stock_abis[i].executable_size)supported=1;
    if(!supported){close(fd);return;}
    void *data=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(data==MAP_FAILED)return;
    unsigned char digest[32];char hex[65];
    unsigned char *(*sha)(const void*,size_t,unsigned char*)=dlsym(RTLD_DEFAULT,"SHA256");
    if(!sha){munmap(data,st.st_size);return;}sha(data,st.st_size,digest);munmap(data,st.st_size);
    for(int j=0;j<32;j++)sprintf(hex+j*2,"%02x",digest[j]);
    for(size_t i=0;i<sizeof(stock_abis)/sizeof(stock_abis[0]);i++)
        if((size_t)st.st_size==stock_abis[i].executable_size&&!strcmp(hex,stock_abis[i].sha256))abi=&stock_abis[i];
    if(!abi)return;
    if(access("/tmp/stock-ui.guard",F_OK)==0){stock_log("previous extension did not exit cleanly; keeping factory UI\n");return;}
    started=milliseconds();arm_guard();
    dl_iterate_phdr(locate,NULL);
    stock_log("supported original GUI %s verified, base=%lx\n",abi->version,base);
}
unsigned stock_ui_tick(unsigned sleep_us){
    if(!base)return sleep_us;
    pump();unsigned cap=preview||inline_show?22000:500000;
    return sleep_us>cap?cap:sleep_us;
}

__attribute__((destructor)) static void cleanup(void){
    if(session){engine->destroy(session);session=NULL;}
    if(guard_armed){unlink("/tmp/stock-ui.guard");guard_armed=0;}
}
