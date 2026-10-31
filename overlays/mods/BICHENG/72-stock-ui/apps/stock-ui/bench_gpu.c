#include "gpu.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FRAMES 240
#define PIXEL_BYTES (WIDTH*HEIGHT*4)
static uint8_t pixels[PIXEL_BYTES],shared[PIXEL_BYTES],display[PIXEL_BYTES],baseline[PIXEL_BYTES];
static unsigned edge_pixels;
static uint8_t colors[3][3];

void stock_log(const char *format,...){
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
}
static void require(int ok,const char *message){
    if(!ok){fprintf(stderr,"%s\n",message);exit(1);}
}
static unsigned number(const char *s,unsigned maximum){
    char *end;errno=0;unsigned long n=strtoul(s,&end,10);
    require(!errno&&end!=s&&!*end&&n>0&&n<=maximum,"Invalid layer or frame count");
    return (unsigned)n;
}
static double clock_ms(void){
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
}
static Gpu *create_gpu(const Model *m){
    char error[128]={0};Gpu *g=gpu_create(m,error,sizeof(error));require(g!=NULL,error);return g;
}
static void check_quality(void){
    Vertex vertices[6];float xs[]={-15,-5,-5,5,5,15};
    for(int i=0;i<6;i++)vertices[i]=(Vertex){.x=xs[i],.y=0,.z=i/2,.color={255,40,40,255}};
    size_t ends[]={2,4,6};float zs[]={0,1,2};
    Model m={.vertices=vertices,.count=6,.capacity=6,.layer_end=ends,.layer_z=zs,.layers=3,
             .min={-15,0,0},.max={15,0,2}};
    RenderView v={.layer=1,.flat=1,.width=WIDTH,.height=HEIGHT,.zoom=1,.completed=SIZE_MAX};
    Gpu *g=create_gpu(&m);require(!gpu_render(g,&m,&v,pixels),"Quality render failed");
    for(size_t i=3;i<sizeof(pixels);i+=4)require(pixels[i]==255,"Output alpha is not opaque");
    const int xs_sample[]={100,240,380};
    for(int i=0;i<3;i++){
        int strongest=-256;
        for(int y=157;y<163;y++){
            uint8_t *p=pixels+(y*WIDTH+xs_sample[i])*4;int contrast=p[2]-p[1];
            if(contrast>strongest){strongest=contrast;memcpy(colors[i],p,3);}
        }
    }
    require(colors[1][2]>235&&colors[1][2]-colors[1][1]>190,"Current layer lost opacity");
    for(int i=0;i<3;i+=2)
        require(colors[i][2]>40&&colors[i][2]<110&&colors[i][2]-colors[i][1]>15,"Context opacity changed");
    v.yaw=.3f;require(!gpu_render(g,&m,&v,pixels),"Antialias render failed");
    for(size_t i=48*WIDTH*4;i<264*WIDTH*4;i+=4)
        if(pixels[i+2]>110&&pixels[i+2]<245&&pixels[i+2]-pixels[i+1]>60)edge_pixels++;
    require(edge_pixels>20,"Antialias coverage missing");gpu_destroy(g);
}
static void measure(Gpu *g,const Model *m,RenderView *v,unsigned frames,int segmented,double values[][6]){
    GpuProfile profile={0};gpu_profile(g,segmented?&profile:NULL);
    for(unsigned i=0;i<frames+5;i++){
        v->yaw=-.45f+i*.01f;
        double start=clock_ms();require(!gpu_render(g,m,v,pixels),"Measured render failed");
        double copy_start=clock_ms();
        memcpy(shared,pixels,sizeof(shared));
        __asm__ volatile("" : : "r"(shared) : "memory");
        memcpy(display,shared,sizeof(display));
        __asm__ volatile("" : : "r"(display) : "memory");
        double end=clock_ms();
        if(i>=5){
            double row[]={copy_start-start,profile.draw_ms,profile.resolve_readback_ms,
                          profile.cpu_prepare_ms,end-copy_start,end-start};
            memcpy(values[i-5],row,sizeof(row));
        }
    }
    gpu_profile(g,NULL);
}
static int compare(const void *a,const void *b){
    double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);
}
static void summary(const double values[][6],unsigned frames,int segmented){
    const char *names[]={"render_ms","draw_finish_ms","resolve_readback_ms","cpu_prepare_ms","delivery_copy_ms","total_ms"};
    putchar('{');int first=1;
    for(unsigned column=0;column<6;column++){
        if(!segmented&&column>0&&column<4)continue;
        double sorted[MAX_FRAMES];for(unsigned i=0;i<frames;i++)sorted[i]=values[i][column];
        qsort(sorted,frames,sizeof(*sorted),compare);
        double median=(sorted[(frames-1)/2]+sorted[frames/2])/2;
        printf("%s\"%s\":{\"median\":%.4f,\"p95\":%.4f}",first?"":",",names[column],median,sorted[(frames*95+99)/100-1]);
        first=0;
    }
    putchar('}');
}
int main(int argc,char **argv){
    if(argc<2||argc>4){fprintf(stderr,"Usage: %s FILE.gcode [LAYER_1_BASED [FRAMES_1_TO_240]]\n",argv[0]);return 2;}
    unsigned requested=argc>2?number(argv[2],20000):0;
    unsigned frames=argc>3?number(argv[3],MAX_FRAMES):40;
    Model m={0};atomic_uint generation=1;
    require(!gcode_load(&m,argv[1],&generation,1),m.error);
    require(requested<=m.layers,"Selected layer is outside the model");
    check_quality();Gpu *g=create_gpu(&m);
    RenderView v={.layer=requested?requested-1:m.layers-1,.width=WIDTH,.height=HEIGHT,
                  .yaw=-.45f,.tilt=.75f,.zoom=1,.completed=SIZE_MAX};
    double plain[MAX_FRAMES][6],segmented[MAX_FRAMES][6];
    measure(g,&m,&v,frames,0,plain);memcpy(baseline,pixels,sizeof(baseline));
    measure(g,&m,&v,frames,1,segmented);
    require(!memcmp(baseline,pixels,sizeof(pixels)),"Profiling changed rendered pixels");
    printf("{\"vertices\":%zu,\"layers\":%u,\"selected_layer\":%u,\"frames\":%u,\"warmup_frames\":5,",m.count,m.layers,v.layer+1,frames);
    printf("\"quality\":{\"opaque_output\":true,\"context_current_context_bgr\":[[%u,%u,%u],[%u,%u,%u],[%u,%u,%u]],\"antialias_edge_pixels\":%u,\"profile_pixels_identical\":true},",
           colors[0][0],colors[0][1],colors[0][2],colors[1][0],colors[1][1],colors[1][2],colors[2][0],colors[2][1],colors[2][2],edge_pixels);
    printf("\"baseline\":");summary(plain,frames,0);printf(",\"segmented\":");summary(segmented,frames,1);puts("}");
    gpu_destroy(g);gcode_free(&m);return 0;
}
