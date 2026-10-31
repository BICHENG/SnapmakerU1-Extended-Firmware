#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static char request_path[PATH_MAX], output_path[PATH_MAX];
#define CAPTURE_REQUEST_PATH request_path
#define CAPTURE_OUTPUT_PATH output_path
#include "capture.c"

static uint8_t display_bytes[1024], first_pixels[FRAME_BYTES], second_pixels[FRAME_BYTES];
static stock_draw_buf_t buffers[2];
static stock_draw_sw_unit_t unit;
static stock_layer_t layer;
static stock_fill_dsc_t descriptor;
static stock_task_t task;
static unsigned calls, removals, locks;
static bool locked;
static char root_path[PATH_MAX];

static void set_pointer(void *base,size_t offset,void *value) { memcpy((uint8_t *)base+offset,&value,sizeof(value)); }
static void set_word(void *base,size_t offset,uint32_t value) { memcpy((uint8_t *)base+offset,&value,sizeof(value)); }
static void mock_lock(void) { assert(!locked); locked=true; locks++; }
static void mock_unlock(void) { assert(locked); locked=false; }
static int32_t original_evaluate(stock_draw_unit_t *u,stock_task_t *t)
{
    assert(locked && u==&unit.base); calls++;
    t->preferred_unit=7; t->preference_score=83;
    return 37;
}
static int32_t foreign_evaluate(stock_draw_unit_t *u,stock_task_t *t) { (void)u; (void)t; return 9; }
static uint32_t mock_remove(void *display,void (*callback)(NativeEvent *),void *user_data)
{
    assert(locked && display==display_bytes && callback==display_event && user_data==&capture);
    removals++;
    return 1;
}
static void event(unsigned code)
{
    NativeEvent e={.target=display_bytes,.original=display_bytes,.code=code,.user_data=&capture};
    mock_lock(); display_event(&e); mock_unlock();
}
static void fixture(const char *name)
{
    assert(!locked);
    free(capture.storage);
    memset(&capture,0,sizeof(capture));
    memset(display_bytes,0,sizeof(display_bytes));
    memset(&unit,0,sizeof(unit));
    memset(&layer,0,sizeof(layer));
    memset(&descriptor,0,sizeof(descriptor));
    memset(&task,0,sizeof(task));
    memset(first_pixels,0x11,sizeof(first_pixels));
    memset(second_pixels,0x22,sizeof(second_pixels));
    for(unsigned i=0;i<2;i++) {
        buffers[i]=(stock_draw_buf_t){.header={.magic=0x19,.cf=STOCK_CF_XRGB8888,.width=WIDTH,.height=HEIGHT,.stride=STRIDE},
            .data_size=FRAME_BYTES,.data=i?second_pixels:first_pixels};
    }
    set_word(display_bytes,0,WIDTH); set_word(display_bytes,4,HEIGHT);
    set_pointer(display_bytes,32,&buffers[0]); set_pointer(display_bytes,40,&buffers[1]); set_pointer(display_bytes,48,&buffers[0]);
    set_word(display_bytes,84,1); set_word(display_bytes,92,STOCK_CF_XRGB8888); set_pointer(display_bytes,752,&layer);
    layer.draw_buf=&buffers[0]; layer.buf_area=(stock_area_t){0,0,WIDTH-1,HEIGHT-1}; layer.color_format=STOCK_CF_XRGB8888;
    descriptor.base.layer=&layer; descriptor.base.dsc_size=sizeof(descriptor); descriptor.opacity=255; descriptor.red=20;
    task.type=1; task.draw_dsc=&descriptor; task.area=(stock_area_t){10,20,39,49};
    task.real_area=task.clip_area=task.clip_original=task.area;
    unit.base.evaluate_cb=observe_evaluate;
    capture.unit=&unit; capture.original=original_evaluate; capture.display=display_bytes;
    capture.storage=calloc(1,sizeof(Storage)); assert(capture.storage);
    capture.lock=mock_lock; capture.unlock=mock_unlock; capture.remove_event=mock_remove;
    capture.binary_verified=true; capture.event_added=true; capture.phase=ARMED;
    capture.deadline=now_ns()+UINT64_C(3000000000);
    calls=removals=locks=0;
    int count=snprintf(output_path,sizeof(output_path),"%s/%s",root_path,name);
    assert(count>0 && (size_t)count<sizeof(output_path));
    count=snprintf(request_path,sizeof(request_path),"%s/request",root_path);
    assert(count>0 && (size_t)count<sizeof(request_path));
}
static void submit(void)
{
    mock_lock(); assert(observe_evaluate(&unit.base,&task)==37); mock_unlock();
}
static void finish(void)
{
    set_word(display_bytes,76,1); event(59);
    stock_draw_tick(0,display_bytes);
    assert(!locked && capture.phase==DONE && unit.base.evaluate_cb==original_evaluate && removals==1 && !capture.storage);
}
static void write_request(const char *text)
{
    assert(!locked);
    FILE *f=fopen(request_path,"wb"); assert(f);
    assert(fwrite(text,1,strlen(text),f)==strlen(text)); assert(!fclose(f));
}
static void verify_report(const char *fragment)
{
    char path[PATH_MAX], text[4096];
    int count=snprintf(path,sizeof(path),"%s/report.json",output_path); assert(count>0 && (size_t)count<sizeof(path));
    FILE *f=fopen(path,"rb"); assert(f);
    size_t size=fread(text,1,sizeof(text)-1,f); text[size]='\0'; assert(!ferror(f)); assert(!fclose(f));
    assert(strstr(text,fragment));
}
static void successful_frame(void)
{
    fixture("complete");
    event(56); assert(capture.phase==RECORDING);
    submit(); assert(calls==1 && task.preferred_unit==7 && task.preference_score==83);
    assert(capture.task_count==1 && capture.storage->tasks[0].evaluate_result==37);
    assert(!memcmp(capture.storage->descriptors,&descriptor,sizeof(descriptor)));
    descriptor.red=90;
    assert(((stock_fill_dsc_t *)capture.storage->descriptors)->red==20);
    memset(first_pixels,0x44,sizeof(first_pixels));
    event(59); assert(capture.phase==RECORDING && capture.flushes==1);
    set_word(display_bytes,76,1); event(59); assert(capture.phase==READY && capture.flushes==2);
    assert(capture.storage->initial[0]==0x11 && capture.storage->final[0]==0x44);
    set_pointer(display_bytes,48,&buffers[1]); memset(first_pixels,0x99,sizeof(first_pixels));
    event(57); assert(capture.phase==READY);
    stock_draw_tick(0,display_bytes);
    assert(capture.phase==DONE && !capture.storage && capture.callback_restored && removals==1);
    assert(unit.base.evaluate_cb==original_evaluate);
    verify_report("\"complete\":true");
    unsigned old_locks=locks;
    write_request("1\n"); stock_draw_tick(0,display_bytes);
    assert(locks==old_locks && access(request_path,F_OK)==0);
    assert(!unlink(request_path));
}
static void rejected_captures(void)
{
    fixture("unknown-type"); event(56); task.type=99; submit();
    assert(capture.phase==FAILED && calls==1); stock_draw_tick(0,display_bytes);
    assert(removals==1 && unit.base.evaluate_cb==original_evaluate); verify_report("unknown_task_abi");
    fixture("unknown-descriptor"); event(56); descriptor.base.dsc_size=120; submit();
    assert(capture.phase==FAILED); stock_draw_tick(0,display_bytes); verify_report("unknown_descriptor_abi");
    fixture("capacity"); event(56); capture.descriptor_bytes=DESC_BYTES-48; submit();
    assert(capture.phase==FAILED); capture.descriptor_bytes=0; stock_draw_tick(0,display_bytes); verify_report("capture_capacity");
    fixture("timeout"); capture.deadline=0; stock_draw_tick(0,display_bytes);
    assert(capture.phase==DONE && removals==1 && unit.base.evaluate_cb==original_evaluate); verify_report("capture_timeout");
    fixture("changed-buffer"); event(56); submit(); set_pointer(display_bytes,48,&buffers[1]); finish();
    verify_report("final_buffer_changed");
    fixture("missing-flush"); event(56); submit(); event(57); stock_draw_tick(0,display_bytes); verify_report("missing_final_flush");
    fixture("changed-callback"); event(56); submit(); set_word(display_bytes,76,1); event(59);
    unit.base.evaluate_cb=foreign_evaluate; stock_draw_tick(0,display_bytes);
    assert(unit.base.evaluate_cb==foreign_evaluate && capture.storage && removals==1);
    verify_report("observer_callback_changed");
    fixture("display-delete"); event(38); stock_draw_tick(0,NULL);
    assert(capture.phase==DONE && capture.callback_restored && removals==0 && !capture.storage);
    verify_report("display_deleted");
}
static void request_checks(void)
{
    fixture("unsupported"); free(capture.storage); memset(&capture,0,sizeof(capture));
    stock_draw_tick(0,NULL); assert(capture.phase==IDLE);
    assert(consume_request(request_path)==0);
    write_request("1\n"); assert(consume_request(request_path)==1 && access(request_path,F_OK)!=0);
    write_request("yes\n"); assert(consume_request(request_path)==-1);
    assert(!mkfifo(request_path,0600)); uint64_t start=now_ns(); assert(consume_request(request_path)==-1);
    assert(now_ns()-start<UINT64_C(100000000));
    assert(!symlink("/dev/zero",request_path)); assert(consume_request(request_path)==-1); assert(!unlink(request_path));
    write_request("1\n"); capture.next_poll=0; stock_draw_tick(0,NULL);
    assert(capture.phase==DONE && !capture.unit && !capture.storage); verify_report("unsupported_binary");
}
int main(int argc,char **argv)
{
    assert(argc==2 && strlen(argv[1])<sizeof(root_path)); strcpy(root_path,argv[1]);
    assert(!mkdir(root_path,0700));
    successful_frame(); rejected_captures(); request_checks();
    fixture("existing"); event(56); submit(); set_word(display_bytes,76,1); event(59);
    mock_lock(); detach_locked(); mock_unlock();
    assert(!mkdir(output_path,0700)); assert(!write_capture(output_path));
    free(capture.storage); capture.storage=NULL;
    puts("capture host checks passed");
    return 0;
}
