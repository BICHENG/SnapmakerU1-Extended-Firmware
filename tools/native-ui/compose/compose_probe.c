#include "vendor_entry.inc"
#include "glyph_capture.inc"

static uint64_t now_ns(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))fatal("clock_failed");
    return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
}
static stock_label_iterate_fn iterate_label;
static stock_label_draw_fn native_label;
static stock_glyph_callback_fn native_glyph;
static void (*label_init)(stock_label_dsc_t *);
static stock_area_t full_clip={0,0,COMPOSE_W-1,COMPOSE_H-1};

typedef struct { const char *name;int kind,opacity; } Scene;
static const Scene scenes[]={
    {"ascii_repeated",0,255},{"wrapped",1,255},{"multiline_spacing",2,255},
    {"clipped_offset",3,255},{"translucent",4,96},{"selection",5,255},
    {"opacity_0",6,0},{"opacity_2",6,2},{"opacity_3",6,3},{"opacity_64",6,64},
    {"opacity_128",6,128},{"opacity_252",6,252},{"opacity_253",6,253},{"opacity_255",6,255},
    {"dense_page",7,255}
};
static void color(uint8_t out[3],uint32_t rgb){out[0]=(uint8_t)rgb;out[1]=(uint8_t)(rgb>>8);out[2]=(uint8_t)(rgb>>16);}
static void fill(stock_draw_unit_t *unit,stock_area_t area,uint32_t rgb,int opacity,int capture) {
    stock_fill_dsc_t d;vendor_fill_init(&d);d.base.layer=unit->target_layer;
    if(d.base.dsc_size!=sizeof(d))fatal("fill_descriptor_size");
    d.blue=(uint8_t)rgb;d.green=(uint8_t)(rgb>>8);d.red=(uint8_t)(rgb>>16);d.opacity=(uint8_t)opacity;
    if(capture)glyph_collect_callback(unit,NULL,&d,&area);else vendor_fill(unit,&d,&area);
}
static void label(stock_draw_unit_t *unit,const char *text,stock_area_t area,stock_area_t clip,
                  uint32_t rgb,int opacity,int variant,int capture) {
    stock_label_dsc_t d;label_init(&d);d.base.layer=unit->target_layer;d.text=text;
    if(d.base.dsc_size!=sizeof(d)||!d.font)fatal("label_descriptor_or_default_font");
    color(d.color,rgb);d.opacity=(uint8_t)opacity;
    if(variant==2){d.letter_space=1;d.line_space=3;}
    if(variant==3){d.offset_x=-9;d.offset_y=-3;}
    if(variant==5){d.selection_start=1;d.selection_end=4;color(d.selection_color,0xf438b1);color(d.selection_background,0x2781c9);}
    unit->clip_area=&clip;
    if(capture){if(opacity>2)iterate_label(unit,&d,&area,glyph_collect_callback);}
    else native_label(unit,&d,&area);
    unit->clip_area=&full_clip;
}
static void render_scene(const Scene *s,stock_draw_unit_t *unit,int capture) {
    unit->clip_area=&full_clip;
    fill(unit,full_clip,0x29435b,255,capture);
    fill(unit,(stock_area_t){0,160,479,319},0xe8e1b4,255,capture);
    if(s->kind<6) {
        const char *text=s->kind==1?"Snapmaker U1 AVAVA 0123 second line":
            s->kind==2?"Snapmaker\nU1 AVAVA":"AVAVA U1 0123456789";
        int width=s->kind==1?91:224;
        label(unit,text,(stock_area_t){25,40,25+width-1,160},
              s->kind==3?(stock_area_t){38,44,184,85}:full_clip,0x3793e7,s->opacity,s->kind,capture);
    }
    else if(s->kind==6) {
        static const uint32_t colors[]={0xf10c67,0x11e6a3,0x4c6fe9};
        for(unsigned i=0;i<3;i++) {
            int delta=(int)i*7;
            fill(unit,(stock_area_t){25+delta,30+delta,250+delta,145+delta},colors[i],s->opacity,capture);
            label(unit,"AVAVA U1 0123456789",(stock_area_t){38+delta,61+delta,370,140},full_clip,
                  colors[2-i],s->opacity,0,capture);
            label(unit,"Snapmaker U1",(stock_area_t){45+delta,183+delta,350,280},full_clip,
                  colors[i],s->opacity,0,capture);
        }
    }
    else {
        for(unsigned i=0;i<12;i++) {
            int x=8+(int)(i%2)*240,y=8+(int)(i/2)*51;
            fill(unit,(stock_area_t){x,y,x+224,y+43},0x19272f+(i*0x050409),255,capture);
            label(unit,"U1 AVAVA 0123456789",(stock_area_t){x+4,y+9,x+220,y+40},full_clip,
                  i%2?0xf0d199:0x93c7ef,(i%3)?255:128,0,capture);
        }
    }
}
static void replay(stock_draw_unit_t *unit,const glyph_probe_capture_t *capture) {
    for(unsigned i=0;i<capture->count;i++) {
        const glyph_probe_event_t *e=&capture->events[i];unit->clip_area=&e->clip;
        native_glyph(unit,e->has_glyph?(stock_glyph_dsc_t *)&e->glyph:NULL,
                     e->has_fill?(stock_fill_dsc_t *)&e->fill:NULL,e->has_fill?&e->fill_area:NULL);
    }
    unit->clip_area=&full_clip;
}
static unsigned commands(const glyph_probe_capture_t *capture,ComposeCommand *out) {
    unsigned count=0;
    for(unsigned i=0;i<capture->count;i++) {
        const glyph_probe_event_t *e=&capture->events[i];
        if(e->has_glyph&&e->has_fill)fatal("unsupported_combined_callback");
        if(e->has_fill) {
            const stock_fill_dsc_t *d=&e->fill;
            if(d->radius)fatal("unexpected_round_fill");
            for(size_t j=0;j<sizeof(d->gradient);j++)if(d->gradient[j])fatal("unexpected_gradient");
            out[count++]=(ComposeCommand){.area=e->fill_area,.clip=e->clip,
                .red=d->red,.green=d->green,.blue=d->blue,.opacity=d->opacity};
        }
        else if(e->pixels) {
            out[count++]=(ComposeCommand){.area=e->letter,.clip=e->clip,
                .red=e->glyph.color[2],.green=e->glyph.color[1],.blue=e->glyph.color[0],.opacity=e->glyph.opacity,
                .mask=e->pixels,.mask_width=e->bitmap.header.width,.mask_height=e->bitmap.header.height,
                .mask_stride=e->bitmap.header.stride};
        }
        else if(e->has_glyph&&e->glyph.format)fatal("unexpected_non_bitmap_glyph");
    }
    return count;
}
static void seed(uint8_t *data) {
    for(unsigned y=0;y<COMPOSE_H;y++)for(unsigned x=0;x<COMPOSE_W;x++) {
        uint8_t *p=data+y*COMPOSE_STRIDE+x*4;p[0]=(uint8_t)(x*7+y);p[1]=(uint8_t)(x+y*11);
        p[2]=(uint8_t)(x*13+y*3);p[3]=255;
    }
}
typedef struct { size_t changed_pixels,changed_rgb,changed_x,first;unsigned max_error; } Difference;
static Difference difference(const uint8_t *actual,const uint8_t *expected) {
    Difference d={.first=SIZE_MAX};
    for(size_t i=0;i<COMPOSE_BYTES;i+=4) {
        int pixel_changed=0;
        for(unsigned c=0;c<4;c++)if(actual[i+c]!=expected[i+c]) {
            if(d.first==SIZE_MAX)d.first=i+c;
            if(c==3)d.changed_x++;else d.changed_rgb++;
            unsigned delta=(unsigned)abs((int)actual[i+c]-(int)expected[i+c]);
            if(delta>d.max_error)d.max_error=delta;
            pixel_changed=1;
        }
        d.changed_pixels+=(unsigned)pixel_changed;
    }
    return d;
}
static void write_diff(FILE *f,Difference d,const uint8_t *actual,const uint8_t *expected) {
    fprintf(f,"{\"exact\":%s,\"changed_pixels\":%zu,\"changed_rgb_bytes\":%zu,\"changed_x_bytes\":%zu,\"max_channel_error\":%u,\"first\":",
        d.changed_pixels?"false":"true",d.changed_pixels,d.changed_rgb,d.changed_x,d.max_error);
    if(d.first==SIZE_MAX)fputs("null",f);
    else fprintf(f,"{\"x\":%zu,\"y\":%zu,\"channel_bgrx\":%zu,\"actual\":%u,\"expected\":%u}",
        (d.first/4)%COMPOSE_W,d.first/COMPOSE_STRIDE,d.first%4,actual[d.first],expected[d.first]);
    fputc('}',f);
}
static int compare_double(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void write_samples(FILE *f,const double *values,unsigned n) {
    double sorted[100];memcpy(sorted,values,n*sizeof(*values));qsort(sorted,n,sizeof(*sorted),compare_double);
    fprintf(f,"{\"median_us\":%.3f,\"p95_us\":%.3f,\"samples_us\":[",sorted[n/2],sorted[(n*95+99)/100-1]);
    for(unsigned i=0;i<n;i++)fprintf(f,"%s%.3f",i?",":"",values[i]);
    fputs("]}",f);
}
static void ppm(const char *directory,const char *scene,const char *mode,const uint8_t *data) {
    char name[512];int n=snprintf(name,sizeof(name),"%s/%s-%s.ppm",directory,scene,mode);
    if(n<0||(size_t)n>=sizeof(name))fatal("output_path_long");
    FILE *f=fopen(name,"wb");if(!f)fatal("output_file_open");fprintf(f,"P6\n480 320\n255\n");
    for(size_t i=0;i<COMPOSE_BYTES;i+=4){uint8_t rgb[]={data[i+2],data[i+1],data[i]};if(fwrite(rgb,1,3,f)!=3)fatal("output_write");}
    if(fclose(f))fatal("output_close");
}
static int probe_main(int argc,char **argv,char **envp) {
    (void)envp;signal(SIGSEGV,crash);signal(SIGBUS,crash);signal(SIGABRT,crash);signal(SIGALRM,crash);alarm(40);
    const char *enabled=getenv("U1_VENDOR_DRAW_PROBE");if(!enabled||strcmp(enabled,"1"))fatal("explicit_probe_mode_required");
    verify_binary(original_main_address);
    unsigned iterations=20;const char *directory=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--output")&&i+1<argc)directory=argv[++i];
        else if(!strcmp(argv[i],"--iterations")&&i+1<argc){char *end;unsigned long n=strtoul(argv[++i],&end,10);if(*end||n<1||n>100)fatal("iterations_1_to_100");iterations=(unsigned)n;}
        else fatal("usage_output_and_iterations");
    }
    if(!directory)fatal("private_output_directory_required");
    ((void (*)(void))(vendor_base+STOCK_LV_INIT_OFFSET))();
    uintptr_t global=*(uintptr_t *)(vendor_base+STOCK_GLOBAL_GOT_OFFSET);
    stock_draw_sw_unit_t *registered=*(stock_draw_sw_unit_t **)(global+STOCK_UNIT_HEAD_OFFSET);
    if(!registered||*(uint32_t *)(global+STOCK_UNIT_COUNT_OFFSET)!=1||registered->task_active||
       (uintptr_t)registered->base.dispatch_cb!=vendor_base+STOCK_SW_DISPATCH_OFFSET)fatal("software_unit_registration");
    label_init=(void (*)(stock_label_dsc_t *))(vendor_base+STOCK_LABEL_INIT_OFFSET);
    iterate_label=(stock_label_iterate_fn)(vendor_base+STOCK_LABEL_ITERATE_OFFSET);
    native_label=(stock_label_draw_fn)(vendor_base+STOCK_SW_LABEL_OFFSET);
    native_glyph=(stock_glyph_callback_fn)(vendor_base+STOCK_SW_GLYPH_CALLBACK_OFFSET);
    char error[256]={0};ComposeGpu *gpu=compose_create(error,sizeof(error));
    if(!gpu){fprintf(stderr,"%s\n",error);fatal("gpu_create");}
    enum { CPU_GUARD=4096 };
    uint8_t *allocation=malloc(COMPOSE_BYTES+CPU_GUARD*2),*expected=malloc(COMPOSE_BYTES),*output=malloc(COMPOSE_BYTES);
    glyph_probe_capture_t *capture=calloc(1,sizeof(*capture));ComposeCommand *cmd=calloc(GLYPH_MAX_EVENTS,sizeof(*cmd));
    if(!allocation||!expected||!output||!capture||!cmd)fatal("allocation_failed");
    memset(allocation,0xa7,COMPOSE_BYTES+CPU_GUARD*2);uint8_t *cpu=allocation+CPU_GUARD;
    stock_draw_buf_t buffer={.header={.magic=0x19,.cf=STOCK_CF_XRGB8888,.flags=2,.width=480,.height=320,.stride=1920},
        .data_size=COMPOSE_BYTES,.data=cpu,.unaligned_data=cpu};
    stock_layer_t layer={.draw_buf=&buffer,.buf_area={0,0,479,319},.color_format=STOCK_CF_XRGB8888,.clip_area={0,0,479,319},.physical_clip_area={0,0,479,319}};
    stock_draw_sw_unit_t unit={.base={.target_layer=&layer,.clip_area=&full_clip}};
    char result_path[512];if(snprintf(result_path,sizeof(result_path),"%s/result.json",directory)>=(int)sizeof(result_path))fatal("output_path");
    FILE *report=fopen(result_path,"w");if(!report)fatal("report_file");
    fprintf(report,"{\"ok\":true,\"vendor_sha256\":\"%s\",\"original_main_called\":false,\"iterations\":%u,\"capabilities\":",STOCK_GUI_SHA256,iterations);
    compose_capabilities(gpu,report);fputs(",\"cases\":[",report);
    int standard_exact=1,fetch_exact=compose_has_fetch(gpu);
    for(unsigned c=0;c<sizeof(scenes)/sizeof(scenes[0]);c++) {
        const Scene *scene=&scenes[c];buffer.data=buffer.unaligned_data=cpu;seed(cpu);render_scene(scene,&unit.base,0);memcpy(expected,cpu,COMPOSE_BYTES);
        seed(cpu);memset(capture,0,sizeof(*capture));capture->original=native_glyph;glyph_capture=capture;
        render_scene(scene,&unit.base,1);glyph_capture=NULL;
        if(memcmp(cpu,expected,COMPOSE_BYTES))fatal("capture_differs_from_native_label");
        seed(cpu);replay(&unit.base,capture);if(memcmp(cpu,expected,COMPOSE_BYTES))fatal("retained_replay_differs_from_native");
        unsigned count=commands(capture,cmd);uint64_t prep_start=now_ns();
        if(compose_prepare(gpu,cmd,count))fatal("prepare_atlas");
        double prepare_us=(now_ns()-prep_start)/1000.0;
        uint8_t *dma=compose_cpu_begin(gpu);if(!dma)fatal("initial_cpu_begin");seed(dma);if(compose_cpu_end(gpu))fatal("initial_cpu_end");
        double cpu_native[100],cpu_replay[100],cpu_dma[100];
        for(unsigned i=0;i<iterations;i++) {
            buffer.data=buffer.unaligned_data=cpu;uint64_t start=now_ns();render_scene(scene,&unit.base,0);memcpy(output,cpu,COMPOSE_BYTES);cpu_native[i]=(now_ns()-start)/1000.0;
            start=now_ns();replay(&unit.base,capture);memcpy(output,cpu,COMPOSE_BYTES);cpu_replay[i]=(now_ns()-start)/1000.0;
            start=now_ns();dma=compose_cpu_begin(gpu);if(!dma)fatal("cpu_rw_begin");buffer.data=buffer.unaligned_data=dma;
            replay(&unit.base,capture);memcpy(output,dma,COMPOSE_BYTES);if(compose_cpu_end(gpu))fatal("cpu_rw_end_or_guard");cpu_dma[i]=(now_ns()-start)/1000.0;
        }
        if(memcmp(output,expected,COMPOSE_BYTES))fatal("dma_cpu_replay_differs_from_native");
        for(size_t i=0;i<CPU_GUARD;i++)if(allocation[i]!=0xa7||allocation[CPU_GUARD+COMPOSE_BYTES+i]!=0xa7)fatal("cpu_guard_overwrite");
        int save=scene->kind==5||!strcmp(scene->name,"opacity_128")||scene->kind==7;
        if(save)ppm(directory,scene->name,"native",expected);
        fprintf(report,"%s{\"name\":\"%s\",\"opacity\":%d,\"commands\":%u,\"glyphs\":%u,\"copied_bitmap_bytes\":%zu,\"capture_and_replay_exact\":true,\"cpu_rw_exact\":true,\"prepare_us\":%.3f,\"cpu_native_and_copy\":",
            c?",":"",scene->name,scene->opacity,count,capture->bitmap_count,capture->bitmap_bytes,prepare_us);
        write_samples(report,cpu_native,iterations);fputs(",\"cpu_replay_and_copy\":",report);write_samples(report,cpu_replay,iterations);
        fputs(",\"cpu_dma_rw_replay_and_copy\":",report);write_samples(report,cpu_dma,iterations);
        for(int fetch=0;fetch<2;fetch++) {
            fprintf(report,",\"%s\":",fetch?"fetch":"standard");
            if(fetch&&!compose_has_fetch(gpu)){fputs("null",report);continue;}
            ComposeTiming t;double submit[100],finish[100],read[100],total[100];
            for(unsigned i=0;i<3;i++)if(compose_render(gpu,fetch,output,&t))fatal("warmup_render_or_guard");
            for(unsigned i=0;i<iterations;i++) {
                if(compose_render(gpu,fetch,output,&t))fatal("render_sync_or_guard");
                submit[i]=t.submit_us;finish[i]=t.finish_us;read[i]=t.read_copy_us;total[i]=t.total_us;
            }
            Difference diff=difference(output,expected);if(diff.changed_pixels){if(fetch)fetch_exact=0;else standard_exact=0;}
            fputs("{\"pixels\":",report);write_diff(report,diff,output,expected);
            fputs(",\"submit\":",report);write_samples(report,submit,iterations);fputs(",\"finish\":",report);write_samples(report,finish,iterations);
            fputs(",\"sync_read_copy\":",report);write_samples(report,read,iterations);fputs(",\"total\":",report);write_samples(report,total,iterations);
            fputs(",\"finish_calls_per_batch\":1,\"gl_read_pixels_calls\":0,\"guards_unchanged\":true}",report);
            if(save)ppm(directory,scene->name,fetch?"fetch":"standard",output);
        }
        fputc('}',report);fflush(report);
        for(unsigned i=0;i<capture->count;i++)free(capture->events[i].pixels);
    }
    fprintf(report,"],\"standard_all_pixels_exact\":%s,\"fetch_all_pixels_exact\":%s,\"timing_scope\":\"warm atlas; complete ordered batch plus finish/sync/full-frame copy; prepare_us is separate; CPU native includes original label layout\"}\n",
        standard_exact?"true":"false",fetch_exact?"true":"false");
    if(fclose(report))fatal("report_close");
    compose_destroy(gpu);free(cmd);free(capture);free(output);free(expected);free(allocation);alarm(0);
    dprintf(report_fd,"{\"ok\":true,\"original_main_called\":false,\"cases\":15,\"standard_exact\":%s,\"fetch_exact\":%s}\n",standard_exact?"true":"false",fetch_exact?"true":"false");
    _exit(0);
}
#include "vendor_start.inc"
