#define _GNU_SOURCE
#include "gpu2d.h"
#include "native_dispatch.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uintptr_t base;
static void *original_main;
static UiGpu2D *gpu;
static NativeDispatch adapter;
static int report_fd;
static void fail(const char *reason) { dprintf(report_fd,"{\"ok\":false,\"reason\":\"%s\"}\n",reason);_exit(2); }
static int find_base(struct dl_phdr_info *info,size_t n,void *unused) {
    (void)n;(void)unused;
    if(info->dlpi_name&&!info->dlpi_name[0]){base=info->dlpi_addr;return 1;}return 0;
}
static void identity(void) {
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);struct stat st;
    if(fd<0||fstat(fd,&st)||st.st_size!=6563736)fail("vendor_size");
    void *data=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(data==MAP_FAILED)fail("vendor_map");
    unsigned char *(*sha)(const void *,size_t,unsigned char *)=dlsym(RTLD_DEFAULT,"SHA256");
    unsigned char digest[32];char hex[65];
    if(!sha||!sha(data,st.st_size,digest))fail("vendor_sha");
    munmap(data,st.st_size);
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
    if(strcmp(hex,STOCK_GUI_SHA256))fail("vendor_identity");
    dl_iterate_phdr(find_base,NULL);
    if(!base||(uintptr_t)original_main!=base+STOCK_MAIN_OFFSET)fail("vendor_main");
}
static int32_t dispatch(stock_draw_unit_t *unit,stock_layer_t *layer) {
    return native_gpu_dispatch(&adapter,unit,layer);
}
static void gpu_task(stock_draw_unit_t *unit,unsigned type,void *dsc,const stock_area_t *coords) {
    size_t size=((stock_draw_dsc_base_t *)dsc)->dsc_size;
    void *copy=((void *(*)(size_t))(base+0xbcee0u))(size);
    if(!copy)fail("descriptor_allocation");
    memcpy(copy,dsc,size);
    stock_layer_t *layer=unit->target_layer;
    layer->clip_area=*unit->clip_area;
    stock_task_t *task=((stock_task_t *(*)(stock_layer_t *,const stock_area_t *))(base+0x897e0u))(layer,coords);
    if(!task)fail("task_allocation");
    task->type=type;task->draw_dsc=copy;
    ((void (*)(stock_layer_t *,stock_task_t *))(base+0x89b90u))(layer,task);
    if(task->state!=1||task->preferred_unit!=1)fail("native_evaluation");
    bool (*dispatch_layer)(void *,stock_layer_t *)=(void *)(base+0x898c0u);
    unsigned before=adapter.gpu_tasks;
    if(!dispatch_layer(NULL,layer)||task->state!=3||adapter.gpu_tasks!=before+1||adapter.cpu_tasks)
        fail(adapter.paint.error?adapter.paint.error:"native_dispatch");
    dispatch_layer(NULL,layer);
    if(layer->draw_task_head||adapter.unit->task_active||((int (*)(void))(base+0xbd020u))()!=1)fail("native_cleanup");
}
static void save(const char *name,const void *data,size_t bytes) {
    const char *dir=getenv("GPU2D_PROBE_DIR");if(!dir)fail("output_dir");
    char path[512];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"wb");if(!f||fwrite(data,1,bytes,f)!=bytes||fclose(f))fail("output_write");
}
static int probe_main(int argc,char **argv,char **envp) {
    (void)argc;(void)argv;(void)envp;identity();alarm(25);
    ((void (*)(void))(base+STOCK_LV_INIT_OFFSET))();
    void *display=((void *(*)(int,int))(base+DISPLAY_CREATE_OFFSET))(480,320);if(!display)fail("display");
    ((void (*)(void *,unsigned))(base+DISPLAY_SET_FORMAT_OFFSET))(display,STOCK_CF_XRGB8888);
    ((void (*)(void *))(base+REFR_SET_DISPLAY_OFFSET))(display);
    uint8_t *reference=calloc(1,480*320*4);if(!reference)fail("reference");
    stock_draw_buf_t buffer={.header={.magic=0x19,.cf=STOCK_CF_XRGB8888,.width=480,.height=320,.stride=1920},
        .data_size=480*320*4,.data=reference,.unaligned_data=reference};
    stock_layer_t layer={.draw_buf=&buffer,.buf_area={0,0,479,319},.color_format=STOCK_CF_XRGB8888,
        .clip_area={0,0,479,319},.physical_clip_area={0,0,479,319}};
    stock_area_t full={0,0,479,319};
    stock_draw_unit_t unit={.target_layer=&layer,.clip_area=&full};
    stock_fill_init_fn fill_init=(stock_fill_init_fn)(base+STOCK_FILL_INIT_OFFSET);
    stock_sw_fill_fn cpu_fill=(stock_sw_fill_fn)(base+STOCK_SW_FILL_OFFSET);
    gpu=ui_gpu2d_create(480,320);if(!gpu)fail("gpu_init");
    UiGpu2D *other=ui_gpu2d_create(32,32);if(!other)fail("second_context");
    ui_gpu2d_destroy(other);
    if(!ui_gpu2d_begin(gpu))fail("context_survives_peer_shutdown");
    void *global=*(void **)(base+STOCK_GLOBAL_GOT_OFFSET);
    stock_draw_sw_unit_t *native_unit=*(stock_draw_sw_unit_t **)((uint8_t *)global+STOCK_UNIT_HEAD_OFFSET);
    for(unsigned i=0;i<2000&&!native_unit->initialized;++i)usleep(1000);
    if(!native_gpu_dispatch_init(&adapter,base,gpu,native_unit,&layer,false))fail("dispatch_init");
    adapter.enabled=true;native_unit->base.dispatch_cb=dispatch;
    const struct {stock_area_t area;uint32_t rgb;int radius;unsigned opacity;} fills[]={
        {{0,0,479,319},0x10141a,0,255},{{8,8,471,66},0x2b333a,18,255},
        {{8,78,232,192},0x335577,12,255},{{246,78,471,192},0x1d68b5,20,173},
        {{8,208,471,308},0x292d31,15,255},{{18,250,460,276},0x2055e8,10,255}};
    for(unsigned i=0;i<sizeof(fills)/sizeof(*fills);++i) {
        stock_fill_dsc_t f;fill_init(&f);f.radius=fills[i].radius;f.opacity=fills[i].opacity;
        f.red=fills[i].rgb>>16;f.green=fills[i].rgb>>8;f.blue=fills[i].rgb;
        cpu_fill(&unit,&f,&fills[i].area);gpu_task(&unit,1,&f,&fills[i].area);
    }
    ResourceNativeLabel label;
    ((void (*)(void *))(base+0x85c00u))(&label);label.text="Native GPU: original LVGL font";
    label.color[0]=label.color[1]=label.color[2]=235;label.opacity=255;
    stock_area_t text={24,26,455,62};
    ((void (*)(stock_draw_unit_t *,const void *,const stock_area_t *))(base+0x83440u))(&unit,&label,&text);
    gpu_task(&unit,4,&label,&text);
    label.text="0123456789  ABCDEFGHIJKLMNOP";text=(stock_area_t){26,222,455,246};
    ((void (*)(stock_draw_unit_t *,const void *,const stock_area_t *))(base+0x83440u))(&unit,&label,&text);
    gpu_task(&unit,4,&label,&text);
    uint8_t image_pixels[96*64*4];
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<96;++x){unsigned p=(y*96+x)*4;image_pixels[p]=x*2;image_pixels[p+1]=y*3;image_pixels[p+2]=170;image_pixels[p+3]=255;}
    image_asset_t asset={.header={.magic=0x19,.cf=STOCK_CF_ARGB8888,.width=96,.height=64,.stride=384},.data_size=sizeof(image_pixels),.data=image_pixels};
    image_draw_dsc_t image;((void (*)(void *))(base+IMAGE_INIT_OFFSET))(&image);image.src=&asset;
    stock_area_t image_area={26,98,121,161};
    ((image_draw_fn)(base+IMAGE_SW_OFFSET))(&unit,&image,&image_area);
    gpu_task(&unit,5,&image,&image_area);
    uint8_t packed[96*64*3];
    for(unsigned p=0;p<96*64;++p)memcpy(packed+p*3,image_pixels+p*4,3);
    image_asset_t rgb888_asset=asset;
    rgb888_asset.header.cf=0x0f;rgb888_asset.header.stride=96*3;rgb888_asset.data_size=sizeof(packed);rgb888_asset.data=packed;
    image.src=&rgb888_asset;
    image_area=(stock_area_t){130,98,225,161};
    ((image_draw_fn)(base+IMAGE_SW_OFFSET))(&unit,&image,&image_area);gpu_task(&unit,5,&image,&image_area);
    uint16_t rgb565[96*64];
    for(unsigned p=0;p<96*64;++p)rgb565[p]=((image_pixels[p*4+2]>>3)<<11)|((image_pixels[p*4+1]>>2)<<5)|(image_pixels[p*4]>>3);
    image_asset_t rgb565_asset=asset;
    rgb565_asset.header.cf=0x12;rgb565_asset.header.stride=96*2;rgb565_asset.data_size=sizeof(rgb565);rgb565_asset.data=(const uint8_t *)rgb565;
    image.src=&rgb565_asset;
    image_area=(stock_area_t){270,98,365,161};
    ((image_draw_fn)(base+IMAGE_SW_OFFSET))(&unit,&image,&image_area);gpu_task(&unit,5,&image,&image_area);
    for(unsigned i=0;i<5;++i) {
        NativeBorder border;((void (*)(void *))(base+0x835c0u))(&border);
        border.radius=i?0:12;border.width=2;border.sides=i?1u<<(i-1):15;
        border.color[0]=70;border.color[1]=170;border.color[2]=220;border.opacity=200;
        stock_area_t rect={18+(int)i*90,282,99+(int)i*90,304};
        ((void (*)(stock_draw_unit_t *,const void *,const stock_area_t *))(base+0x6ade0u))(&unit,&border,&rect);
        gpu_task(&unit,2,&border,&rect);
    }
    NativeShadow shadow;((void (*)(void *))(base+0x83620u))(&shadow);
    shadow.radius=10;shadow.width=24;shadow.opacity=200;
    stock_area_t shadow_area={396,98,450,158};
    ((void (*)(stock_draw_unit_t *,const void *,const stock_area_t *))(base+0x81d90u))(&unit,&shadow,&shadow_area);
    gpu_task(&unit,3,&shadow,&shadow_area);
    stock_fill_dsc_t card;fill_init(&card);card.radius=10;card.red=card.green=card.blue=100;
    cpu_fill(&unit,&card,&shadow_area);gpu_task(&unit,1,&card,&shadow_area);
    for(unsigned i=0;i<5;++i) {
        NativeLine line={.base={.dsc_size=sizeof(NativeLine)},.x1=24,.y1=196+i*2,.x2=224,.y2=196+i*2,
            .color={190,180,100},.width=1,.opacity=255};
        if(i==1){line.x1=line.x2=242;line.y1=80;line.y2=190;}
        if(i==2){line.width=2;line.x1=250;line.x2=460;}
        if(i==3){line.width=3;line.x1=250;line.x2=460;line.opacity=170;}
        if(i==4){line.x1=30;line.y1=95;line.x2=200;line.y2=180;line.width=3;line.flags=12;}
        ((void (*)(stock_draw_unit_t *,const void *))(base+0x67ce0u))(&unit,&line);
        gpu_task(&unit,7,&line,&full);
    }
    for(unsigned i=0;i<3;++i) {
        NativeArc arc={.base={.dsc_size=sizeof(NativeArc)},.color={170,90,235},.width=5,
            .start=30,.end=280,.x=60+(int)i*100,.y=120,.radius=24,.opacity=255,.rounded=i==1};
        if(i==2){arc.start=0;arc.end=360;arc.opacity=180;}
        stock_area_t bounds={arc.x-arc.radius,arc.y-arc.radius,arc.x+arc.radius-1,arc.y+arc.radius-1};
        ((void (*)(stock_draw_unit_t *,const void *,const stock_area_t *))(base+0x6c910u))(&unit,&arc,&bounds);
        gpu_task(&unit,8,&arc,&bounds);
    }
    for(unsigned i=0;i<3;++i) {
        stock_area_t bounds={260+(int)i*65,216,319+(int)i*65,268};
        stock_layer_t child={.buf_area=bounds,.clip_area=bounds,.physical_clip_area=bounds,.color_format=STOCK_CF_ARGB8888};
        if(!((void *(*)(stock_layer_t *))(base+STOCK_LAYER_ALLOC_OFFSET))(&child))fail("child_buffer");
        stock_draw_unit_t child_unit={.target_layer=&child,.clip_area=&bounds};
        stock_fill_dsc_t fill;fill_init(&fill);fill.radius=12;fill.red=210;fill.green=90;fill.blue=30;fill.opacity=180;
        cpu_fill(&child_unit,&fill,&bounds);gpu_task(&child_unit,1,&fill,&bounds);
        ResourceNativeLabel text;((void (*)(void *))(base+0x85c00u))(&text);
        text.text="GPU";text.color[0]=text.color[1]=text.color[2]=235;text.opacity=255;
        stock_area_t label_bounds={bounds.x1+4,bounds.y1+10,bounds.x2-4,bounds.y2-4};
        ((void (*)(stock_draw_unit_t *,const void *,const stock_area_t *))(base+0x83440u))(&child_unit,&text,&label_bounds);
        gpu_task(&child_unit,4,&text,&label_bounds);
        image_draw_dsc_t composite;((void (*)(void *))(base+IMAGE_INIT_OFFSET))(&composite);
        composite.src=&child;composite.opacity=130+i*50;
        ((image_draw_fn)(base+0x6c350u))(&unit,&composite,&bounds);
        gpu_task(&unit,6,&composite,&bounds);
        if(child.draw_buf)fail("child_cleanup");
    }
    const uint8_t *actual=ui_gpu2d_finish(gpu);if(!actual)fail("gpu_finish");
    unsigned different=0,large=0,max_error=0;double sum=0;
    for(unsigned p=0;p<480*320;++p){unsigned error=0;for(unsigned c=0;c<3;++c){unsigned e=abs((int)actual[p*4+c]-reference[p*4+c]);sum+=e;if(e>error)error=e;}
        different+=error!=0;large+=error>32;if(error>max_error)max_error=error;}
    save("cpu.bgra",reference,480*320*4);save("gpu.bgra",actual,480*320*4);
    if(!ui_gpu2d_read_end(gpu))fail("gpu_end_sync");
    if(sum/(480*320*3)>.5 || large>768)fail("pixel_comparison");
    if(!ui_gpu2d_begin(gpu))fail("image_scene_begin");
    stock_fill_dsc_t background;fill_init(&background);background.red=background.green=background.blue=0;
    cpu_fill(&unit,&background,&full);gpu_task(&unit,1,&background,&full);
    for(unsigned p=0;p<96*64;++p)image_pixels[p*4+3]=80+(p%96)*175/95;
    image.src=&asset;
    for(unsigned i=0;i<3;++i) {
        image.scale_x=i==0?128:i==1?384:256;image.scale_y=i==0?160:i==1?320:128;
        image.rotation=i==2?370:0;image.pivot_x=i?48:0;image.pivot_y=i?32:0;image.opacity=190;
        stock_area_t bounds={20+(int)i*140,30+(int)i*80,115+(int)i*140,93+(int)i*80};
        ((image_draw_fn)(base+IMAGE_SW_OFFSET))(&unit,&image,&bounds);gpu_task(&unit,5,&image,&bounds);
    }
    const uint8_t *transformed=ui_gpu2d_finish(gpu);if(!transformed)fail("image_scene_finish");
    double image_sum=0;unsigned image_large=0;
    for(unsigned p=0;p<480*320;++p) {
        unsigned worst=0;
        for(unsigned c=0;c<3;++c){unsigned e=abs((int)transformed[p*4+c]-reference[p*4+c]);image_sum+=e;if(e>worst)worst=e;}
        image_large+=worst>32;
    }
    save("image-cpu.bgra",reference,480*320*4);save("image-gpu.bgra",transformed,480*320*4);
    if(!ui_gpu2d_read_end(gpu))fail("image_scene_end_sync");
    dprintf(report_fd,"{\"transformed_image_mean_error\":%.6f,\"transformed_image_pixels_gt_32\":%u}\n",image_sum/(480*320*3),image_large);
    if(image_sum/(480*320*3)>.5||image_large>768)fail("transformed_image_pixels");
    native_unit->base.dispatch_cb=adapter.original;
    dprintf(report_fd,"{\"ok\":true,\"original_main_called\":false,\"native_queue_and_cleanup\":true,\"gpu_tasks\":%u,\"cpu_tasks\":%u,\"glyphs_on_gpu\":%u,\"images_on_gpu\":%u,\"fills_on_gpu\":%u,\"different_pixels\":%u,\"pixels_error_gt_32\":%u,\"max_error\":%u,\"mean_rgb_error\":%.6f}\n",adapter.gpu_tasks,adapter.cpu_tasks,adapter.paint.glyphs,adapter.paint.images,adapter.paint.fills,different,large,max_error,sum/(480*320*3));
    ui_gpu2d_destroy(gpu);free(reference);return 0;
}
__attribute__((visibility("default"))) int __libc_start_main(int (*main_fn)(int,char **,char **),int argc,char **argv,
    void (*init)(void),void (*fini)(void),void (*rtld_fini)(void),void *stack_end) {
    report_fd=dup(STDOUT_FILENO);if(report_fd<0||dup2(STDERR_FILENO,STDOUT_FILENO)<0)_exit(2);
    original_main=(void *)main_fn;
    int (*start)(int (*)(int,char **,char **),int,char **,void (*)(void),void (*)(void),void (*)(void),void *)=dlsym(RTLD_NEXT,"__libc_start_main");
    if(!start)_exit(2);
    return start(probe_main,argc,argv,init,fini,rtld_fini,stack_end);
}
