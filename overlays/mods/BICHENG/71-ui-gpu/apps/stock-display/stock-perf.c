#define _POSIX_C_SOURCE 200809L
#include "stock-perf.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    EVENT_DELETE = 38, REFR_START = 54, REFR_READY = 55,
    RENDER_START = 56, RENDER_READY = 57, FLUSH_START = 58, FLUSH_FINISH = 59,
    WAIT_START = 60, WAIT_FINISH = 61, EVENT_CAPACITY = 1024, SAMPLE_CAPACITY = 1024
};
#define WINDOW_NS UINT64_C(5000000000)
#define BUDGET_NS UINT64_C(22200000)

typedef struct {
    void *target, *original;
    uint32_t code;
    void *user_data, *param, *prev;
    uint8_t flags;
} NativeEvent;
_Static_assert(sizeof(NativeEvent) == 56, "verified 2.1 event ABI");
_Static_assert(offsetof(NativeEvent, code) == 16, "verified event code offset");
_Static_assert(offsetof(NativeEvent, param) == 32, "verified event parameter offset");

typedef struct {
    uint64_t ns, pixels;
    unsigned epoch;
    uint16_t code, invalid;
} EventRecord;
typedef struct {
    uint64_t ended, refresh, render, flush, wait, pixels;
    unsigned flushes, rendered;
} Sample;
typedef struct {
    Sample sample;
    uint64_t started, render_started, flush_started, wait_started;
    int active, rendering, flushing, waiting, invalid;
} Pending;

static struct {
    atomic_uint head, tail, dropped, epoch;
    atomic_uintptr_t display;
    EventRecord events[EVENT_CAPACITY];
    uintptr_t base;
    int configured, gpu_enabled;
    unsigned refresh_period_ms, animation_period_ms, dropped_at_config;
    uint64_t configured_ns;
} source;
static struct {
    unsigned epoch, first, count, sequence_errors, sample_losses, write_failures;
    unsigned dropped_at_start;
    uint64_t started, last_write;
    Pending pending;
    Sample samples[SAMPLE_CAPACITY];
} window;

static uint64_t perf_now(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void display_event(NativeEvent *event)
{
    if((uintptr_t)event->target != atomic_load_explicit(&source.display, memory_order_relaxed)) return;
    unsigned code = event->code & 0x7fffu;
    if(code == EVENT_DELETE) {
        atomic_store_explicit(&source.display, 0, memory_order_release);
        return;
    }
    if(code < REFR_START || code > WAIT_FINISH) return;
    unsigned head = atomic_load_explicit(&source.head, memory_order_relaxed);
    if(head - atomic_load_explicit(&source.tail, memory_order_acquire) >= EVENT_CAPACITY) {
        atomic_fetch_add_explicit(&source.dropped, 1, memory_order_relaxed);
        return;
    }
    EventRecord record = {.ns=perf_now(), .epoch=atomic_load_explicit(&source.epoch, memory_order_relaxed), .code=code};
    if(!record.ns) record.invalid = 1;
    if(code == FLUSH_START) {
        if(!event->param) record.invalid = 1;
        else {
            const int32_t *area = event->param;
            int64_t width = (int64_t)area[2] - area[0] + 1;
            int64_t height = (int64_t)area[3] - area[1] + 1;
            if(width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX) record.invalid = 1;
            else record.pixels = (uint64_t)width * (uint64_t)height;
        }
    }
    /* LVGL's lock serializes producers; release/acquire protects the unlocked consumer. */
    source.events[head % EVENT_CAPACITY] = record;
    atomic_store_explicit(&source.head, head + 1, memory_order_release);
}

int stock_perf_attach(uintptr_t base, const char *verified_version, void *display,
                      int gpu_enabled, unsigned refresh_period_ms, unsigned animation_period_ms)
{
    if(!verified_version) return -1;
    if(strcmp(verified_version, "2.1.0.157")) return 0;
    if(!base || !display || !refresh_period_ms) return -1;
    uintptr_t attached = atomic_load_explicit(&source.display, memory_order_acquire);
    if(attached && (attached != (uintptr_t)display || source.base != base)) return -1;
    int gpu = !!gpu_enabled;
    if(attached && gpu == source.gpu_enabled && refresh_period_ms == source.refresh_period_ms &&
       animation_period_ms == source.animation_period_ms) return 1;
    uint64_t now = perf_now();
    if(!now) return -1;
    source.base = base;
    source.gpu_enabled = gpu;
    source.refresh_period_ms = refresh_period_ms;
    source.animation_period_ms = animation_period_ms;
    source.configured_ns = now;
    source.dropped_at_config = atomic_load_explicit(&source.dropped, memory_order_relaxed);
    source.configured = 1;
    atomic_fetch_add_explicit(&source.epoch, 1, memory_order_release);
    if(!attached) {
        atomic_store_explicit(&source.display, (uintptr_t)display, memory_order_release);
        void (*add_event)(void *, void (*)(NativeEvent *), uint32_t, void *) =
            (void (*)(void *, void (*)(NativeEvent *), uint32_t, void *))(base + 0xcb7b0u);
        add_event(display, display_event, 0, NULL);
    }
    return 1;
}

static void append_sample(const Sample *sample)
{
    while(window.count && sample->ended - window.samples[window.first].ended >= WINDOW_NS) {
        window.first = (window.first + 1) % SAMPLE_CAPACITY;
        window.count--;
    }
    if(window.count == SAMPLE_CAPACITY) {
        window.first = (window.first + 1) % SAMPLE_CAPACITY;
        window.count--;
        window.sample_losses++;
    }
    window.samples[(window.first + window.count++) % SAMPLE_CAPACITY] = *sample;
}

static void consume_event(const EventRecord *event)
{
    Pending *p = &window.pending;
    if(event->code == REFR_START) {
        if(p->active) window.sequence_errors++;
        memset(p, 0, sizeof(*p));
        p->active = 1; p->started = event->ns; p->invalid = event->invalid;
        return;
    }
    if(!p->active) { window.sequence_errors++; return; }
    if(event->invalid || event->ns < p->started) p->invalid = 1;
    switch(event->code) {
    case RENDER_START:
        if(p->rendering || p->sample.rendered) p->invalid = 1;
        p->rendering = 1; p->render_started = event->ns;
        break;
    case RENDER_READY:
        if(!p->rendering || p->flushing || p->waiting) p->invalid = 1;
        p->rendering = 0; p->sample.rendered = 1;
        p->sample.render += event->ns - p->render_started;
        break;
    case FLUSH_START:
        if(!p->rendering || p->flushing) p->invalid = 1;
        p->flushing = 1; p->flush_started = event->ns;
        p->sample.pixels += event->pixels; p->sample.flushes++;
        break;
    case FLUSH_FINISH:
        if(!p->flushing) p->invalid = 1;
        p->flushing = 0; p->sample.flush += event->ns - p->flush_started;
        break;
    case WAIT_START:
        if(p->waiting) p->invalid = 1;
        p->waiting = 1; p->wait_started = event->ns;
        break;
    case WAIT_FINISH:
        if(!p->waiting) p->invalid = 1;
        p->waiting = 0; p->sample.wait += event->ns - p->wait_started;
        break;
    case REFR_READY:
        if(p->rendering || p->flushing || p->waiting) p->invalid = 1;
        p->sample.ended = event->ns; p->sample.refresh = event->ns - p->started;
        if(p->invalid) window.sequence_errors++;
        else append_sample(&p->sample);
        p->active = 0;
        break;
    default: break;
    }
}

static int compare_duration(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

static void write_percentiles(FILE *file, const char *name, uint64_t *values, unsigned count)
{
    fprintf(file, ",\"%s\":", name);
    if(!count) { fputs("null", file); return; }
    qsort(values, count, sizeof(*values), compare_duration);
    fprintf(file, "{\"p50\":%.4f,\"p95\":%.4f}", values[(count*50+99)/100-1]/1e6,
            values[(count*95+99)/100-1]/1e6);
}

static int write_snapshot(uint64_t now)
{
    uint64_t refresh[SAMPLE_CAPACITY], render[SAMPLE_CAPACITY], flush[SAMPLE_CAPACITY], wait[SAMPLE_CAPACITY];
    unsigned frames=0, idle=0, over_budget=0, flushes=0;
    uint64_t pixels=0, flush_total=0, wait_total=0;
    for(unsigned i=0;i<window.count;i++) {
        const Sample *sample=&window.samples[(window.first+i)%SAMPLE_CAPACITY];
        pixels+=sample->pixels; flushes+=sample->flushes;
        flush_total+=sample->flush; wait_total+=sample->wait;
        if(!sample->rendered) { idle++; continue; }
        refresh[frames]=sample->refresh; render[frames]=sample->render;
        flush[frames]=sample->flush; wait[frames]=sample->wait;
        if(sample->refresh>BUDGET_NS) over_budget++;
        frames++;
    }
    uint64_t span=now-window.started;
    if(span>WINDOW_NS) span=WINDOW_NS;
    unsigned dropped=atomic_load_explicit(&source.dropped,memory_order_relaxed)-window.dropped_at_start;
    char temporary[]="/tmp/.stock-ui-perf.XXXXXX";
    int fd=mkstemp(temporary);
    if(fd<0) return -1;
    FILE *file=fdopen(fd,"w");
    if(!file) { close(fd); unlink(temporary); return -1; }
    fprintf(file,"{\"version\":\"2.1.0.157\",\"mode\":\"gpu_%s\",\"gpu_output_enabled\":%s,"
        "\"stock_control_renderer\":\"cpu\","
        "\"attached\":%s,\"refresh_period_ms\":%u,\"window_ms\":%.3f,\"monotonic_ms\":%.3f,"
        "\"rendered_frames\":%u,\"actual_fps\":%.4f,\"idle_refreshes\":%u,\"refreshes\":%u,"
        "\"submitted_pixels\":%" PRIu64 ",\"flush_calls\":%u,\"flush_callback_total_ms\":%.4f,"
        "\"flush_wait_total_ms\":%.4f,\"frame_budget_ms\":22.2,\"over_budget_frames\":%u,\"over_budget_percent\":%.3f,"
        "\"complete\":%s,\"dropped_events\":%u,\"sequence_errors\":%u,\"sample_losses\":%u,\"write_failures\":%u",
        source.gpu_enabled?"on":"off",source.gpu_enabled?"true":"false",
        atomic_load_explicit(&source.display,memory_order_acquire)?"true":"false",source.refresh_period_ms,
        span/1e6,now/1e6,frames,span?frames*1e9/span:0.0,idle,frames+idle,pixels,flushes,
        flush_total/1e6,wait_total/1e6,over_budget,frames?over_budget*100.0/frames:0.0,
        dropped||window.sequence_errors||window.sample_losses?"false":"true",dropped,window.sequence_errors,
        window.sample_losses,window.write_failures);
    if(source.animation_period_ms) fprintf(file,",\"animation_period_ms\":%u",source.animation_period_ms);
    else fputs(",\"animation_period_ms\":null",file);
    write_percentiles(file,"drawn_frame_refresh_ms",refresh,frames);
    write_percentiles(file,"render_span_ms",render,frames);
    write_percentiles(file,"flush_callback_ms",flush,frames);
    write_percentiles(file,"flush_wait_ms",wait,frames);
    fputs(",\"timing_scope\":\"refresh_includes_layout_sync_render_flush; render_span_includes_flush_wait; "
          "flush_finish_is_callback_return_not_panel_scanout\"}\n",file);
    int failed=ferror(file);
    if(fclose(file)) failed=1;
    if(!failed && !rename(temporary,"/tmp/stock-ui-perf.json")) return 0;
    unlink(temporary);
    return -1;
}

void stock_perf_poll(void)
{
    if(!source.configured) return;
    uint64_t now=perf_now();
    if(!now) return;
    unsigned epoch=atomic_load_explicit(&source.epoch,memory_order_acquire);
    if(window.epoch!=epoch) {
        memset(&window,0,sizeof(window)); window.epoch=epoch;
        window.started=window.last_write=source.configured_ns;
        window.dropped_at_start=source.dropped_at_config;
    }
    unsigned tail=atomic_load_explicit(&source.tail,memory_order_relaxed);
    unsigned head=atomic_load_explicit(&source.head,memory_order_acquire);
    while(tail!=head) {
        EventRecord event=source.events[tail% EVENT_CAPACITY];
        atomic_store_explicit(&source.tail,++tail,memory_order_release);
        if(event.epoch==epoch) consume_event(&event);
    }
    now=perf_now();
    if(!now) return;
    while(window.count && now>=window.samples[window.first].ended && now-window.samples[window.first].ended>=WINDOW_NS) {
        window.first=(window.first+1)%SAMPLE_CAPACITY; window.count--;
    }
    if(now-window.last_write<WINDOW_NS) return;
    window.last_write=now;
    if(write_snapshot(now)) window.write_failures++;
}
