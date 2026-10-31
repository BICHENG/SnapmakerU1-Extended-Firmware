#include "vendor_entry.inc"

enum { WIDTH=240, HEIGHT=96, STRIDE=WIDTH*4+20, GUARD=64,
       PIXEL_BYTES=STRIDE*HEIGHT, TOTAL_BYTES=PIXEL_BYTES+2*GUARD,
       EVENT_CAPACITY=512, ARENA_CAPACITY=512*1024 };

typedef struct {
    uint8_t memory[TOTAL_BYTES];
    stock_draw_buf_t buffer;
    stock_layer_t layer;
    stock_draw_sw_unit_t unit;
} Target;
typedef struct {const char *name;unsigned kind,format;} Case;
static const Case cases[]={
    {"label_wrapped_translucent",0,STOCK_CF_ARGB8888},
    {"label_selection",1,STOCK_CF_XRGB8888},
    {"label_multiline_clipped",2,STOCK_CF_ARGB8888},
    {"label_opacity_2",3,STOCK_CF_ARGB8888},
    {"argb_scaled_clipped",4,STOCK_CF_ARGB8888},
    {"xrgb_scaled_to_argb",5,STOCK_CF_ARGB8888},
    {"a8_tiled_translucent",6,STOCK_CF_XRGB8888},
    {"argb_rotated_clipped",7,STOCK_CF_XRGB8888}
};

static uint8_t seed_byte(size_t offset) {return (uint8_t)(offset*29+83);}
static void target_init(Target *target,unsigned format,const stock_area_t *clip)
{
    memset(target,0,sizeof(*target));
    for(size_t i=0;i<TOTAL_BYTES;i++) target->memory[i]=seed_byte(i);
    target->buffer=(stock_draw_buf_t){.header={.magic=0x19,.cf=(uint8_t)format,.width=WIDTH,
        .height=HEIGHT,.stride=STRIDE},.data_size=PIXEL_BYTES,
        .data=target->memory+GUARD,.unaligned_data=target->memory+GUARD};
    target->layer=(stock_layer_t){.draw_buf=&target->buffer,.buf_area={0,0,WIDTH-1,HEIGHT-1},
        .color_format=format,.clip_area=*clip,.physical_clip_area=*clip};
    target->unit.base.target_layer=&target->layer;target->unit.base.clip_area=clip;
}
static size_t exact_differences(const Target *a,const Target *b)
{
    size_t differences=0;
    for(size_t i=0;i<TOTAL_BYTES;i++) differences+=a->memory[i]!=b->memory[i];
    return differences;
}
static size_t visible_differences(const Target *a,const Target *b,unsigned format,size_t *alpha)
{
    size_t differences=0;*alpha=0;
    for(unsigned y=0;y<HEIGHT;y++) for(unsigned x=0;x<WIDTH;x++) for(unsigned channel=0;channel<4;channel++) {
        if(channel==3 && format==STOCK_CF_XRGB8888) continue;
        size_t offset=GUARD+y*STRIDE+x*4+channel;
        bool differs=a->memory[offset]!=b->memory[offset];differences+=differs;
        if(channel==3) *alpha+=differs;
    }
    return differences;
}
static size_t invalid_writes(const Target *target,const stock_area_t *clip)
{
    size_t changed=0;
    for(size_t i=0;i<TOTAL_BYTES;i++) {
        bool outside=i<GUARD || i>=GUARD+PIXEL_BYTES;
        if(!outside) {
            size_t offset=i-GUARD;int x=(int)((offset%STRIDE)/4),y=(int)(offset/STRIDE);
            outside=offset%STRIDE>=WIDTH*4 || x<clip->x1 || x>clip->x2 || y<clip->y1 || y>clip->y2;
        }
        if(outside) changed+=target->memory[i]!=seed_byte(i);
    }
    return changed;
}
static size_t visible_changes(const Target *target,unsigned format)
{
    size_t changed=0;
    for(unsigned y=0;y<HEIGHT;y++) for(unsigned x=0;x<WIDTH;x++) for(unsigned channel=0;channel<4;channel++) {
        if(channel==3 && format==STOCK_CF_XRGB8888) continue;
        size_t offset=GUARD+y*STRIDE+x*4+channel;changed+=target->memory[offset]!=seed_byte(offset);
    }
    return changed;
}
static void native_draw(Target *target,stock_task_t *task)
{
    stock_draw_dsc_base_t *base=task->draw_dsc;base->layer=&target->layer;
    if(task->type==4) ((void (*)(stock_draw_unit_t *,const ResourceNativeLabel *,const stock_area_t *))
        (vendor_base+0x83440u))(&target->unit.base,task->draw_dsc,&task->area);
    else ((image_draw_fn)(vendor_base+IMAGE_SW_OFFSET))(&target->unit.base,task->draw_dsc,&task->area);
}
static int capture_draw(ResourceCapture *capture,Target *target,stock_task_t *task,unsigned sequence)
{
    ((stock_draw_dsc_base_t *)task->draw_dsc)->layer=&target->layer;
    return resource_capture_draw(capture,vendor_base,sequence,1,&target->unit.base,task);
}
static bool run_case(FILE *report,const Case *test,unsigned index)
{
    stock_area_t clip={9,7,WIDTH-12,HEIGHT-10};
    Target *reference=malloc(sizeof(*reference)),*forward=malloc(sizeof(*forward)),*replay=malloc(sizeof(*replay)),
        *limited=malloc(sizeof(*limited));
    ResourceEvent *events=calloc(EVENT_CAPACITY,sizeof(*events));uint8_t *arena=malloc(ARENA_CAPACITY);
    if(!reference || !forward || !replay || !limited || !events || !arena) fatal("case_allocation_failed");
    target_init(reference,test->format,&clip);target_init(forward,test->format,&clip);target_init(replay,test->format,&clip);
    ResourceNativeLabel label;image_draw_dsc_t image;image_asset_t asset;
    char text[128]="Snapmaker U1 AVAVA 0123456789 second line";
    uint64_t copied_font[8]={0};uint8_t source[9*64];memset(source,0,sizeof(source));
    stock_task_t task={.area={17,13,184,85},.clip_area=clip};
    if(test->kind<4) {
        ((void (*)(ResourceNativeLabel *))(vendor_base+0x85c00u))(&label);
        if(label.base.dsc_size!=sizeof(label) || !label.font || label.opacity!=255) fatal("native_label_init_mismatch");
        memcpy(copied_font,label.font,sizeof(copied_font));label.font=copied_font;label.text=text;
        label.color[0]=37;label.color[1]=181;label.color[2]=239;
        if(test->kind==0) {task.area.x2=108;label.opacity=117;}
        if(test->kind==1) {
            label.selection_start=1;label.selection_end=8;
            label.selection_color[0]=19;label.selection_color[1]=103;label.selection_color[2]=217;
            label.selection_background[0]=201;label.selection_background[1]=43;label.selection_background[2]=71;
        }
        if(test->kind==2) {
            strcpy(text,"Snapmaker\nU1 AVAVA");label.line_space=3;label.letter_space=1;
            label.offset_x=-11;label.offset_y=-3;clip=(stock_area_t){23,17,123,64};
            target_init(reference,test->format,&clip);target_init(forward,test->format,&clip);target_init(replay,test->format,&clip);
            task.clip_area=clip;
        }
        if(test->kind==3) label.opacity=2;
        task.type=4;task.draw_dsc=&label;
    }
    else {
        ((void (*)(image_draw_dsc_t *))(vendor_base+IMAGE_INIT_OFFSET))(&image);
        if(image.base.dsc_size!=sizeof(image)) fatal("native_image_init_mismatch");
        unsigned format=test->kind==5?STOCK_CF_XRGB8888:test->kind==6?IMAGE_CF_A8:STOCK_CF_ARGB8888;
        unsigned stride=format==IMAGE_CF_A8?20:60;
        for(size_t i=0;i<sizeof(source);i++) source[i]=(uint8_t)(i*47+19);
        asset=(image_asset_t){.header={.magic=0x19,.cf=(uint8_t)format,.width=13,.height=9,.stride=(uint16_t)stride},
            .data_size=stride*9,.data=source};
        image.src=&asset;image.header=asset.header;image.opacity=177;
        image.recolor[0]=37;image.recolor[1]=181;image.recolor[2]=239;
        image.pivot_x=6;image.pivot_y=4;task.area=(stock_area_t){15,11,27,19};
        if(test->kind==4) {image.scale_x=384;image.scale_y=320;}
        if(test->kind==5) {image.scale_x=160;image.scale_y=192;}
        if(test->kind==6) {image.flags|=32;task.area=(stock_area_t){7,5,74,44};}
        if(test->kind==7) image.rotation=370;
        task.type=5;task.draw_dsc=&image;
    }
    native_draw(reference,&task);
    ResourceCapture capture;resource_capture_init(&capture,events,EVENT_CAPACITY,arena,ARENA_CAPACITY);
    if(capture_draw(&capture,forward,&task,index)!=1) fatal("normal_capture_not_consumed");
    size_t forwarded_diff=exact_differences(reference,forward);
    unsigned saved_events=capture.event_count,saved_bytes=capture.byte_count,saved_errors=capture.errors;
    unsigned saved_callbacks=capture.native_callbacks;
    size_t capacity_diff[2]={0};unsigned capacity_errors[2]={0};bool capacity_ok=true;
    for(unsigned limit=0;limit<2;limit++) {
        target_init(limited,test->format,&clip);
        ResourceCapture small;ResourceEvent single_event;
        resource_capture_init(&small,limit?&single_event:NULL,limit?1:0,arena,0);
        if(capture_draw(&small,limited,&task,index)!=1) fatal("limited_capture_not_consumed");
        capacity_diff[limit]=exact_differences(reference,limited);capacity_errors[limit]=small.errors;
        bool opacity_skip=test->kind==3;
        capacity_ok&=capacity_diff[limit]==0 && (opacity_skip?small.errors==0:(small.errors&RESOURCE_ERROR_CAPACITY)!=0);
    }
    memset(text,0xd3,sizeof(text));memset(copied_font,0xd3,sizeof(copied_font));
    memset(source,0xd3,sizeof(source));memset(&asset,0xd3,sizeof(asset));
    memset(&label,0xd3,sizeof(label));memset(&image,0xd3,sizeof(image));task.draw_dsc=NULL;
    bool replay_ok=saved_errors==0;
    if(replay_ok) for(unsigned i=0;i<saved_events;i++)
        if(!resource_replay_event(vendor_base,&replay->unit.base,&events[i],arena,saved_bytes)) {replay_ok=false;break;}
    size_t alpha_diff=0,replay_diff=visible_differences(reference,replay,test->format,&alpha_diff);
    size_t bad_writes=invalid_writes(reference,&clip)+invalid_writes(forward,&clip)+invalid_writes(replay,&clip);
    size_t changed=visible_changes(reference,test->format);
    bool expect_pixels=test->kind!=3;
    bool ok=forwarded_diff==0 && replay_ok && replay_diff==0 && !bad_writes && capacity_ok &&
        (expect_pixels?(changed>0 && saved_events>0):(changed==0 && saved_events==0));
    fprintf(report,"%s{\"name\":\"%s\",\"target_cf\":%u,\"ok\":%s,\"capture_errors\":%u,"
        "\"events\":%u,\"asset_bytes\":%u,\"native_callbacks\":%u,\"forward_exact_mismatch_bytes\":%zu,"
        "\"replay_valid_pixel_mismatch_bytes\":%zu,\"replay_argb_alpha_mismatch_bytes\":%zu,"
        "\"invalid_clip_padding_guard_writes\":%zu,\"changed_valid_bytes\":%zu,"
        "\"zero_events_mismatch_bytes\":%zu,\"zero_events_errors\":%u,"
        "\"zero_bytes_mismatch_bytes\":%zu,\"zero_bytes_errors\":%u,\"poisoned_before_replay\":true}",
        index?",":"",test->name,test->format,ok?"true":"false",saved_errors,saved_events,saved_bytes,saved_callbacks,
        forwarded_diff,replay_diff,alpha_diff,bad_writes,changed,capacity_diff[0],capacity_errors[0],capacity_diff[1],capacity_errors[1]);
    fflush(report);free(reference);free(forward);free(replay);free(limited);free(events);free(arena);return ok;
}
static int probe_main(int argc,char **argv,char **environment)
{
    (void)argc;(void)argv;(void)environment;
    signal(SIGSEGV,crash);signal(SIGBUS,crash);signal(SIGABRT,crash);signal(SIGILL,crash);signal(SIGALRM,crash);alarm(25);
    const char *enabled=getenv("U1_NATIVE_RESOURCES_ISOLATED_PROBE");
    if(!enabled || strcmp(enabled,"1")) fatal("explicit_isolated_probe_mode_required");
    verify_binary();
    ((void (*)(void))(vendor_base+STOCK_LV_INIT_OFFSET))();
    uintptr_t global=*(uintptr_t *)(vendor_base+STOCK_GLOBAL_GOT_OFFSET);
    stock_draw_sw_unit_t *registered=*(stock_draw_sw_unit_t **)(global+STOCK_UNIT_HEAD_OFFSET);
    if(!registered || *(uint32_t *)(global+STOCK_UNIT_COUNT_OFFSET)!=1 || registered->task_active ||
       (uintptr_t)registered->base.dispatch_cb!=vendor_base+STOCK_SW_DISPATCH_OFFSET ||
       (uintptr_t)registered->base.evaluate_cb!=vendor_base+STOCK_SW_EVALUATE_OFFSET) fatal("native_unit_mismatch");
    void *display=((void *(*)(int32_t,int32_t))(vendor_base+DISPLAY_CREATE_OFFSET))(WIDTH,HEIGHT);
    if(!display) fatal("offscreen_display_creation_failed");
    ((void (*)(void *,uint32_t))(vendor_base+DISPLAY_SET_FORMAT_OFFSET))(display,STOCK_CF_ARGB8888);
    ((void (*)(void *))(vendor_base+REFR_SET_DISPLAY_OFFSET))(display);
    if(((void *(*)(void))(vendor_base+REFR_GET_DISPLAY_OFFSET))()!=display) fatal("refresh_context_mismatch");
    FILE *report=fdopen(report_fd,"w");if(!report) fatal("report_open_failed");
    fprintf(report,"{\"vendor_sha256\":\"%s\",\"original_main_called\":false,\"schema\":1,"
        "\"display\":[%d,%d],\"comparison\":\"valid RGB; ARGB alpha required; XRGB X ignored; target guards and padding unchanged\","
        "\"cases\":[",STOCK_GUI_SHA256,WIDTH,HEIGHT);
    bool ok=true;unsigned n=sizeof(cases)/sizeof(cases[0]);
    for(unsigned i=0;i<n;i++) ok=run_case(report,&cases[i],i)&&ok;
    fprintf(report,"],\"case_count\":%u,\"ok\":%s}\n",n,ok?"true":"false");
    fflush(report);_exit(ok?0:1);
}
typedef int (*libc_start_main_fn)(int (*)(int,char **,char **),int,char **,
    void (*)(void),void (*)(void),void (*)(void),void *);
int __libc_start_main(int (*main_fn)(int,char **,char **),int argc,char **argv,
    void (*init)(void),void (*fini)(void),void (*rtld_fini)(void),void *stack_end)
{
    report_fd=dup(STDOUT_FILENO);if(report_fd<0 || dup2(STDERR_FILENO,STDOUT_FILENO)<0) _exit(2);
    original_main_address=(void *)main_fn;
    libc_start_main_fn start=(libc_start_main_fn)dlsym(RTLD_NEXT,"__libc_start_main");
    if(!start) _exit(2);
    return start(probe_main,argc,argv,init,fini,rtld_fini,stack_end);
}
