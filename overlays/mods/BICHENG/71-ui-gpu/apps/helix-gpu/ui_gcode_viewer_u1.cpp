// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_gcode_viewer.h"
#include "gcode-preview.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h"

#include "app_globals.h"
#include "lv_draw_buf_guard.h"
#include "printer_state.h"
#include "theme_manager.h"

#include <helix-xml/src/xml/lv_xml_parser.h>
#include <helix-xml/src/xml/parsers/lv_xml_obj_parser.h>
#include <lvgl/src/others/translation/lv_translation.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <string>
#include <vector>

namespace {
using helix::GcodeViewerRenderMode;
using helix::GcodeViewerState;

const GcodePreviewApi* preview_api() {
    const char* enabled = std::getenv("UI_GPU_ENABLED");
    if (!HELIX_HAS_GCODE_VIEWER || (enabled && std::strcmp(enabled, "0") == 0))
        return nullptr;
    static const GcodePreviewApi* api = [] {
        void* handle = dlopen(GCODE_PREVIEW_LIBRARY, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            spdlog::warn("[U1 Preview] Shared renderer unavailable: {}", dlerror());
            return static_cast<const GcodePreviewApi*>(nullptr);
        }
        auto get_api = reinterpret_cast<const GcodePreviewApi* (*)(uint32_t)>(
            dlsym(handle, "gcode_preview_get_api"));
        const auto* found = get_api ? get_api(GCODE_PREVIEW_ABI) : nullptr;
        if (!found || found->abi != GCODE_PREVIEW_ABI || found->size < sizeof(GcodePreviewApi) ||
            !found->create || !found->destroy || !found->load || !found->clear || !found->pause ||
            !found->viewport || !found->action || !found->poll) {
            spdlog::warn("[U1 Preview] Shared renderer ABI mismatch");
            dlclose(handle);
            return static_cast<const GcodePreviewApi*>(nullptr);
        }
        // The process owns this library handle; sessions may outlive static UI helpers.
        return found;
    }();
    return api;
}

struct Viewer {
    lv_obj_t* object = nullptr;
    lv_obj_t* fullscreen = nullptr;
    lv_obj_t* mode_label = nullptr;
    lv_obj_t* layer_label = nullptr;
    lv_obj_t* follow_button = nullptr;
    lv_obj_t* slider = nullptr;
    lv_timer_t* timer = nullptr;
    lv_draw_buf_t* embedded_frame = nullptr;
    lv_draw_buf_t* fullscreen_frame = nullptr;
    const GcodePreviewApi* api = nullptr;
    GcodePreview* preview = nullptr;
    GcodePreviewStatus status{};
    std::string filename, job_filename, next_job_filename, layer_text;
    std::string estimated_time, next_estimated_time;
    GcodeViewerState phase = GcodeViewerState::Empty;
    GcodeViewerRenderMode mode = GcodeViewerRenderMode::Auto;
    gcode_viewer_load_callback_t load_callback = nullptr, first_callback = nullptr;
    void* load_user_data = nullptr;
    void* first_user_data = nullptr;
    ui_gcode_viewer_clear_cb_t clear_callback = nullptr;
    void* clear_user_data = nullptr;
    bool paused = false, load_notified = false, first_notified = false;
    bool fallback_notified = false, slider_dragging = false, dragged = false;
    bool wants_follow = false;
    lv_point_t last_pointer{}, press_origin{};
};

std::vector<lv_obj_t*> viewers;

Viewer* state(lv_obj_t* object) {
    return object ? static_cast<Viewer*>(lv_obj_get_user_data(object)) : nullptr;
}

void wake(Viewer& viewer) {
    if (viewer.timer)
        lv_timer_resume(viewer.timer);
}

void action(Viewer& viewer, int command, float x = 0, float y = 0) {
    if (viewer.preview) {
        viewer.api->action(viewer.preview, command, x, y);
        wake(viewer);
    }
}

void clear_frames(Viewer& viewer) {
    helix::safe_draw_buf_destroy(viewer.embedded_frame, "u1_preview_card");
    helix::safe_draw_buf_destroy(viewer.fullscreen_frame, "u1_preview_full");
    viewer.first_notified = false;
}

void close_fullscreen(Viewer& viewer) {
    if (!viewer.fullscreen)
        return;
    lv_obj_t* overlay = viewer.fullscreen;
    viewer.fullscreen = nullptr;
    viewer.mode_label = viewer.layer_label = viewer.follow_button = viewer.slider = nullptr;
    viewer.slider_dragging = false;
    viewer.layer_text.clear();
    lv_obj_delete(overlay);
    wake(viewer);
    if (viewer.object)
        lv_obj_invalidate(viewer.object);
}

lv_draw_buf_t* target_frame(Viewer& viewer) {
    lv_obj_t* target = viewer.fullscreen ? viewer.fullscreen : viewer.object;
    const int compact = viewer.fullscreen ? 0 : 1;
    const int width = compact ? std::clamp(lv_obj_get_width(target), 1, 480) : 480;
    const int height = compact ? std::clamp(lv_obj_get_height(target), 1, 320) : 320;
    if (viewer.api->viewport(viewer.preview, width, height, compact, 0) != 0)
        return nullptr;
    auto*& frame = compact ? viewer.embedded_frame : viewer.fullscreen_frame;
    if (!frame || frame->header.w != width || frame->header.h != height) {
        helix::safe_draw_buf_destroy(frame, "u1_preview_resize");
        frame = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, 0);
    }
    return frame;
}

void draw_frame(lv_event_t* event) {
    auto* viewer = static_cast<Viewer*>(lv_event_get_user_data(event));
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* frame = target == viewer->fullscreen ? viewer->fullscreen_frame : viewer->embedded_frame;
    if (!frame || !viewer->status.visible)
        return;
    lv_area_t bounds;
    lv_obj_get_coords(target, &bounds);
    lv_area_t image_area = helix::draw_buf_blit_area(*frame, bounds);
    lv_draw_image_dsc_t image;
    lv_draw_image_dsc_init(&image);
    image.src = frame;
    lv_draw_image(lv_event_get_layer(event), &image, &image_area);
}

void update_controls(Viewer& viewer) {
    if (!viewer.fullscreen)
        return;
    const auto& status = viewer.status;
    char label[160];
    std::snprintf(label, sizeof(label), "%s %u/%u  %.2f mm", lv_tr("Layer"),
                  status.layers ? status.layer + 1 : 0, status.layers, status.layer_height);
    std::string text(label);
    std::string time_text;
    if (status.live_available && !viewer.job_filename.empty() &&
        viewer.job_filename == get_printer_state().get_effective_print_filename()) {
        const int remaining = lv_subject_get_int(get_printer_state().get_print_time_left_subject());
        if (remaining > 0) {
            std::snprintf(label, sizeof(label), "%s %dh%02dm", lv_tr("remaining"),
                          remaining / 3600, (remaining / 60) % 60);
            time_text = label;
        }
    }
    if (time_text.empty() && !viewer.estimated_time.empty())
        time_text = std::string(lv_tr("Est. Duration")) + " " + viewer.estimated_time;
    if (!time_text.empty())
        text += "\n" + time_text;
    if (text != viewer.layer_text) {
        viewer.layer_text = std::move(text);
        lv_label_set_text(viewer.layer_label, viewer.layer_text.c_str());
    }
    const char* mode = status.flat ? "2D" : "3D";
    if (std::strcmp(lv_label_get_text(viewer.mode_label), mode))
        lv_label_set_text(viewer.mode_label, mode);
    if (status.live_follow)
        lv_obj_add_state(viewer.follow_button, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(viewer.follow_button, LV_STATE_CHECKED);
    if (status.live_available)
        lv_obj_remove_state(viewer.follow_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(viewer.follow_button, LV_STATE_DISABLED);
    const int max_layer = status.layers > 1 ? static_cast<int>(status.layers - 1) : 1;
    if (lv_slider_get_max_value(viewer.slider) != max_layer)
        lv_slider_set_range(viewer.slider, 0, max_layer);
    if (!viewer.slider_dragging && lv_slider_get_value(viewer.slider) != static_cast<int>(status.layer))
        lv_slider_set_value(viewer.slider, static_cast<int>(status.layer), LV_ANIM_OFF);
}

void tick(lv_timer_t* timer) {
    auto* viewer = static_cast<Viewer*>(lv_timer_get_user_data(timer));
    if (viewer->phase == GcodeViewerState::Empty) {
        lv_timer_pause(timer);
        return;
    }
    if (viewer->preview && viewer->phase != GcodeViewerState::Error) {
        const bool initial_frame = !viewer->first_notified && !viewer->paused;
        const bool visible = viewer->fullscreen || lv_obj_is_visible(viewer->object);
        const bool render = viewer->phase == GcodeViewerState::Loaded && viewer->load_notified &&
                            !viewer->paused && (visible || initial_frame);
        viewer->api->pause(viewer->preview, render ? 0 : 1);
        auto* frame = render ? target_frame(*viewer) : nullptr;
        if (frame)
            lv_draw_wait_for_finish();
        const int result = render && !frame ? -2 : viewer->api->poll(viewer->preview, &viewer->status,
            frame ? frame->data : nullptr, frame ? frame->data_size : 0,
            frame ? frame->header.stride : 0);
        if (result == -1)
            return;
        if (result == -2 || viewer->status.phase == GCODE_PREVIEW_ERROR) {
            viewer->phase = GcodeViewerState::Error;
        } else if (viewer->status.phase == GCODE_PREVIEW_LOADED) {
            viewer->phase = GcodeViewerState::Loaded;
        }
        if (result == 1) {
            lv_image_cache_drop(frame);
            lv_obj_invalidate(viewer->fullscreen ? viewer->fullscreen : viewer->object);
        }
    }
    if (viewer->phase == GcodeViewerState::Error) {
        if (viewer->preview)
            viewer->api->pause(viewer->preview, 1);
        if (!viewer->fallback_notified) {
            viewer->fallback_notified = true;
            close_fullscreen(*viewer);
            clear_frames(*viewer);
            if (viewer->clear_callback) {
                viewer->clear_callback(viewer->object, viewer->clear_user_data);
                return;
            }
        }
        if (!viewer->load_notified) {
            viewer->load_notified = true;
            if (viewer->load_callback) {
                viewer->load_callback(viewer->object, viewer->load_user_data, false);
                return;
            }
        }
        lv_timer_pause(timer);
        return;
    }
    if (viewer->phase != GcodeViewerState::Loaded)
        return;
    if (!viewer->load_notified) {
        viewer->load_notified = true;
        if (viewer->load_callback) {
            viewer->load_callback(viewer->object, viewer->load_user_data, true);
            return;
        }
    }
    update_controls(*viewer);
    if (!viewer->first_notified && viewer->status.visible &&
        (viewer->embedded_frame || viewer->fullscreen_frame)) {
        viewer->first_notified = true;
        if (viewer->first_callback) {
            viewer->first_callback(viewer->object, viewer->first_user_data, true);
            return;
        }
    }
    if (viewer->paused)
        lv_timer_pause(timer);
}

enum ButtonAction { Back, Flat, ZoomOut, ZoomIn, Follow };

void button_clicked(lv_event_t* event) {
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* viewer = static_cast<Viewer*>(lv_event_get_user_data(event));
    const auto command = static_cast<ButtonAction>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));
    switch (command) {
    case Back:
        close_fullscreen(*viewer);
        break;
    case Flat:
        viewer->mode = viewer->status.flat ? GcodeViewerRenderMode::Render3D : GcodeViewerRenderMode::Layer2D;
        action(*viewer, GCODE_PREVIEW_FLAT, viewer->status.flat ? 0.0f : 1.0f);
        break;
    case ZoomOut:
        action(*viewer, GCODE_PREVIEW_ZOOM, 1.0f / 1.25f);
        break;
    case ZoomIn:
        action(*viewer, GCODE_PREVIEW_ZOOM, 1.25f);
        break;
    case Follow:
        action(*viewer, GCODE_PREVIEW_FOLLOW);
        break;
    }
}

lv_obj_t* button(Viewer& viewer, const char* text, ButtonAction command,
                 lv_align_t alignment, int x, int y, int width) {
    auto* object = lv_button_create(viewer.fullscreen);
    lv_obj_set_size(object, width, 32);
    lv_obj_align(object, alignment, x, y);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x272e34), 0);
    lv_obj_set_style_bg_opa(object, 230, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x1866ff), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 10, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_user_data(object, reinterpret_cast<void*>(static_cast<intptr_t>(command)));
    lv_obj_add_event_cb(object, button_clicked, LV_EVENT_CLICKED, &viewer);
    auto* label = lv_label_create(object);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, theme_manager_get_font("font_heading"), 0);
    lv_obj_center(label);
    if (command == Flat)
        viewer.mode_label = label;
    return object;
}

void slider_event(lv_event_t* event) {
    auto* viewer = static_cast<Viewer*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED)
        viewer->slider_dragging = true;
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        viewer->slider_dragging = false;
    else if (code == LV_EVENT_VALUE_CHANGED) {
        action(*viewer, GCODE_PREVIEW_LAYER, static_cast<float>(lv_slider_get_value(viewer->slider)));
    }
}

void pointer_event(lv_event_t* event);

void fullscreen_deleted(lv_event_t* event) {
    auto* viewer = static_cast<Viewer*>(lv_event_get_user_data(event));
    if (lv_event_get_target(event) != viewer->fullscreen)
        return;
    viewer->fullscreen = nullptr;
    viewer->mode_label = viewer->layer_label = viewer->follow_button = viewer->slider = nullptr;
    viewer->slider_dragging = false;
    viewer->layer_text.clear();
    wake(*viewer);
}

void open_fullscreen(Viewer& viewer) {
    if (viewer.fullscreen || viewer.phase != GcodeViewerState::Loaded || !viewer.embedded_frame)
        return;
    viewer.fullscreen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(viewer.fullscreen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(viewer.fullscreen, 0, 0);
    lv_obj_set_style_border_width(viewer.fullscreen, 0, 0);
    lv_obj_set_style_radius(viewer.fullscreen, 0, 0);
    lv_obj_set_style_bg_color(viewer.fullscreen, lv_color_hex(0x101415), 0);
    lv_obj_remove_flag(viewer.fullscreen, static_cast<lv_obj_flag_t>(
        LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_add_flag(viewer.fullscreen, static_cast<lv_obj_flag_t>(
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(viewer.fullscreen, draw_frame, LV_EVENT_DRAW_MAIN, &viewer);
    lv_obj_add_event_cb(viewer.fullscreen, pointer_event, LV_EVENT_ALL, &viewer);
    lv_obj_add_event_cb(viewer.fullscreen, fullscreen_deleted, LV_EVENT_DELETE, &viewer);
    button(viewer, lv_tr("Back"), Back, LV_ALIGN_TOP_LEFT, 8, 8, 64);
    button(viewer, viewer.status.flat ? "2D" : "3D", Flat, LV_ALIGN_TOP_RIGHT, -112, 8, 48);
    button(viewer, "-", ZoomOut, LV_ALIGN_TOP_RIGHT, -60, 8, 44);
    button(viewer, "+", ZoomIn, LV_ALIGN_TOP_RIGHT, -8, 8, 44);
    viewer.follow_button = button(viewer, lv_tr("Current layer"), Follow,
                                  LV_ALIGN_BOTTOM_RIGHT, -8, -26, 100);
    viewer.layer_label = lv_label_create(viewer.fullscreen);
    lv_obj_set_style_text_font(viewer.layer_label, theme_manager_get_font("font_heading"), 0);
    lv_obj_set_style_text_color(viewer.layer_label, lv_color_hex(0xe5e8eb), 0);
    lv_obj_set_width(viewer.layer_label, 350);
    lv_label_set_long_mode(viewer.layer_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(viewer.layer_label, LV_ALIGN_BOTTOM_LEFT, 12, -28);
    viewer.slider = lv_slider_create(viewer.fullscreen);
    lv_obj_set_size(viewer.slider, 456, 14);
    lv_obj_align(viewer.slider, LV_ALIGN_BOTTOM_MID, 0, -3);
    for (auto part : {LV_PART_MAIN, LV_PART_INDICATOR, LV_PART_KNOB}) {
        lv_obj_set_style_bg_opa(viewer.slider, LV_OPA_TRANSP, part);
        lv_obj_set_style_border_width(viewer.slider, 0, part);
        lv_obj_set_style_shadow_width(viewer.slider, 0, part);
    }
    lv_obj_set_ext_click_area(viewer.slider, 5);
    lv_obj_add_event_cb(viewer.slider, slider_event, LV_EVENT_ALL, &viewer);
    wake(viewer);
    update_controls(viewer);
}

void pointer_event(lv_event_t* event) {
    auto* viewer = static_cast<Viewer*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    lv_indev_t* input = lv_indev_active();
    if (!input || (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
                   code != LV_EVENT_SHORT_CLICKED))
        return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        viewer->last_pointer = point;
        viewer->press_origin = point;
        viewer->dragged = false;
    } else if (code == LV_EVENT_PRESSING) {
        const int dx = point.x - viewer->last_pointer.x, dy = point.y - viewer->last_pointer.y;
        if (std::abs(point.x - viewer->press_origin.x) + std::abs(point.y - viewer->press_origin.y) > 8)
            viewer->dragged = true;
        if (target == viewer->fullscreen && (dx || dy))
            action(*viewer, GCODE_PREVIEW_ROTATE, dx * 0.012f, dy * 0.012f);
        viewer->last_pointer = point;
    } else if (target == viewer->object && !viewer->dragged) {
        open_fullscreen(*viewer);
    }
}

void deleted(lv_event_t* event) {
    auto* object = static_cast<lv_obj_t*>(lv_event_get_target(event));
    std::unique_ptr<Viewer> viewer(state(object));
    if (!viewer)
        return;
    lv_obj_set_user_data(object, nullptr);
    viewer->object = nullptr;
    viewers.erase(std::remove(viewers.begin(), viewers.end(), object), viewers.end());
    if (viewer->timer) {
        lv_timer_delete(viewer->timer);
        viewer->timer = nullptr;
    }
    close_fullscreen(*viewer);
    if (viewer->preview)
        viewer->api->destroy(viewer->preview);
    clear_frames(*viewer);
}
} // namespace

lv_obj_t* ui_gcode_viewer_create(lv_obj_t* parent) {
    auto* object = lv_obj_create(parent);
    auto viewer = std::make_unique<Viewer>();
    viewer->object = object;
    const char* mode = std::getenv("HELIX_GCODE_MODE");
    if (mode && std::strcmp(mode, "2D") == 0)
        viewer->mode = GcodeViewerRenderMode::Layer2D;
    lv_obj_set_user_data(object, viewer.get());
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(
        LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_add_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(object, draw_frame, LV_EVENT_DRAW_MAIN, viewer.get());
    lv_obj_add_event_cb(object, pointer_event, LV_EVENT_ALL, viewer.get());
    lv_obj_add_event_cb(object, deleted, LV_EVENT_DELETE, nullptr);
    viewer->timer = lv_timer_create(tick, 22, viewer.get());
    lv_timer_pause(viewer->timer);
    viewers.push_back(object);
    viewer.release();
    return object;
}

void ui_gcode_viewer_set_job_filename(lv_obj_t* object, const char* filename,
                                    const char* estimated_time) {
    if (auto* viewer = state(object)) {
        viewer->next_job_filename = filename ? filename : "";
        viewer->next_estimated_time = estimated_time ? estimated_time : "";
    }
}

void ui_gcode_viewer_load_file(lv_obj_t* object, const char* path) {
    auto* viewer = state(object);
    if (!viewer)
        return;
    close_fullscreen(*viewer);
    if (viewer->preview)
        viewer->api->clear(viewer->preview);
    clear_frames(*viewer);
    viewer->status = {};
    viewer->phase = GcodeViewerState::Loading;
    viewer->filename = path ? path : "";
    viewer->job_filename = std::move(viewer->next_job_filename);
    viewer->next_job_filename.clear();
    viewer->estimated_time = std::move(viewer->next_estimated_time);
    viewer->next_estimated_time.clear();
    viewer->load_notified = viewer->fallback_notified = false;
    const auto* available_api = preview_api();
    if (!available_api && viewer->preview) {
        viewer->api->destroy(viewer->preview);
        viewer->preview = nullptr;
    }
    viewer->api = available_api;
    if (viewer->api && !viewer->preview)
        viewer->preview = viewer->api->create();
    if (!viewer->preview || viewer->filename.empty()) {
        viewer->phase = GcodeViewerState::Error;
    } else {
        const uint32_t width = static_cast<uint32_t>(std::clamp(lv_obj_get_width(object), 1, 480));
        const uint32_t height = static_cast<uint32_t>(std::clamp(lv_obj_get_height(object), 1, 320));
        const GcodePreviewSource source{viewer->filename.c_str(),
            viewer->job_filename.empty() ? nullptr : viewer->job_filename.c_str(), width, height, 1, 0,
            viewer->wants_follow};
        viewer->api->pause(viewer->preview, 1);
        if (viewer->api->load(viewer->preview, &source) != 0)
            viewer->phase = GcodeViewerState::Error;
        else
            action(*viewer, GCODE_PREVIEW_FLAT,
                   viewer->mode == GcodeViewerRenderMode::Layer2D ? 1.0f : 0.0f);
    }
    wake(*viewer);
}

void ui_gcode_viewer_set_load_callback(lv_obj_t* object, gcode_viewer_load_callback_t callback, void* data) {
    if (auto* viewer = state(object)) {
        viewer->load_callback = callback;
        viewer->load_user_data = data;
    }
}

void ui_gcode_viewer_set_first_frame_callback(lv_obj_t* object, gcode_viewer_load_callback_t callback, void* data) {
    if (auto* viewer = state(object)) {
        viewer->first_callback = callback;
        viewer->first_user_data = data;
    }
}

void ui_gcode_viewer_set_clear_callback(lv_obj_t* object, ui_gcode_viewer_clear_cb_t callback, void* data) {
    if (auto* viewer = state(object)) {
        viewer->clear_callback = callback;
        viewer->clear_user_data = data;
    }
}

void ui_gcode_viewer_clear(lv_obj_t* object) {
    auto* viewer = state(object);
    if (!viewer)
        return;
    close_fullscreen(*viewer);
    if (viewer->preview)
        viewer->api->clear(viewer->preview);
    clear_frames(*viewer);
    viewer->filename.clear();
    viewer->job_filename.clear();
    viewer->next_job_filename.clear();
    viewer->estimated_time.clear();
    viewer->next_estimated_time.clear();
    viewer->status = {};
    viewer->phase = GcodeViewerState::Empty;
    viewer->wants_follow = false;
    lv_timer_pause(viewer->timer);
    lv_obj_invalidate(object);
    if (viewer->clear_callback)
        viewer->clear_callback(object, viewer->clear_user_data);
}

void ui_gcode_viewer_clear_all_active() {
    const auto active = viewers;
    for (auto* viewer : active)
        if (lv_obj_is_valid(viewer))
            ui_gcode_viewer_clear(viewer);
}

bool ui_gcode_viewer_has_content(lv_obj_t* object) {
    auto* viewer = state(object);
    return viewer && viewer->phase == GcodeViewerState::Loaded && viewer->status.layers;
}

void ui_gcode_viewer_set_paused(lv_obj_t* object, bool paused) {
    if (auto* viewer = state(object)) {
        viewer->paused = paused;
        if (paused)
            close_fullscreen(*viewer);
        if (viewer->preview)
            viewer->api->pause(viewer->preview, paused ? 1 : 0);
        wake(*viewer);
    }
}

bool ui_gcode_viewer_is_paused(lv_obj_t* object) {
    auto* viewer = state(object);
    return !viewer || viewer->paused;
}

void ui_gcode_viewer_force_redraw(lv_obj_t* object) {
    if (auto* viewer = state(object)) {
        action(*viewer, GCODE_PREVIEW_REFRESH);
        lv_obj_invalidate(object);
    }
}

void ui_gcode_viewer_set_render_mode(lv_obj_t* object, GcodeViewerRenderMode mode) {
    if (auto* viewer = state(object)) {
        viewer->mode = mode;
        action(*viewer, GCODE_PREVIEW_FLAT, mode == GcodeViewerRenderMode::Layer2D ? 1.0f : 0.0f);
    }
}

bool ui_gcode_viewer_is_using_2d_mode(lv_obj_t* object) {
    auto* viewer = state(object);
    return viewer && viewer->mode == GcodeViewerRenderMode::Layer2D;
}

void ui_gcode_viewer_zoom(lv_obj_t* object, float factor) {
    if (auto* viewer = state(object); viewer && std::isfinite(factor) && factor > 0)
        action(*viewer, GCODE_PREVIEW_ZOOM, factor);
}

void ui_gcode_viewer_reset_camera(lv_obj_t* object) {
    if (auto* viewer = state(object)) {
        action(*viewer, GCODE_PREVIEW_RESET);
        action(*viewer, GCODE_PREVIEW_FLAT, viewer->mode == GcodeViewerRenderMode::Layer2D ? 1.0f : 0.0f);
    }
}

void ui_gcode_viewer_set_view(lv_obj_t* object, helix::GcodeViewerPresetView preset) {
    if (preset == helix::GcodeViewerPresetView::Top)
        ui_gcode_viewer_set_render_mode(object, GcodeViewerRenderMode::Layer2D);
    else {
        ui_gcode_viewer_set_render_mode(object, GcodeViewerRenderMode::Render3D);
        ui_gcode_viewer_reset_camera(object);
        if (auto* viewer = state(object)) {
            if (preset == helix::GcodeViewerPresetView::Front)
                action(*viewer, GCODE_PREVIEW_ROTATE, .45f, .6f);
            else if (preset == helix::GcodeViewerPresetView::Side)
                action(*viewer, GCODE_PREVIEW_ROTATE, 2.0207963f, .6f);
        }
    }
}

void ui_gcode_viewer_set_camera_azimuth(lv_obj_t* object, float degrees) {
    if (auto* viewer = state(object); viewer && viewer->preview && std::isfinite(degrees)) {
        if (viewer->api->poll(viewer->preview, &viewer->status, nullptr, 0, 0) >= 0)
            action(*viewer, GCODE_PREVIEW_ROTATE, degrees * .0174532925f - viewer->status.yaw, 0);
    }
}

void ui_gcode_viewer_set_camera_elevation(lv_obj_t* object, float degrees) {
    if (auto* viewer = state(object); viewer && viewer->preview && std::isfinite(degrees)) {
        if (viewer->api->poll(viewer->preview, &viewer->status, nullptr, 0, 0) >= 0)
            action(*viewer, GCODE_PREVIEW_ROTATE, 0, degrees * .0174532925f - viewer->status.tilt);
    }
}

void ui_gcode_viewer_set_camera_zoom(lv_obj_t* object, float zoom) {
    if (auto* viewer = state(object); viewer && viewer->preview && std::isfinite(zoom) && zoom > 0) {
        if (viewer->api->poll(viewer->preview, &viewer->status, nullptr, 0, 0) >= 0 && viewer->status.zoom > 0)
            action(*viewer, GCODE_PREVIEW_ZOOM, zoom / viewer->status.zoom);
    }
}

void ui_gcode_viewer_set_print_progress(lv_obj_t* object, int layer) {
    if (auto* viewer = state(object)) {
        const bool follow = layer >= 0;
        if (viewer->wants_follow == follow)
            return;
        viewer->wants_follow = follow;
        if (follow)
            action(*viewer, GCODE_PREVIEW_FOLLOW);
        else
            action(*viewer, GCODE_PREVIEW_LAYER, static_cast<float>(viewer->status.layers) - 1);
    }
}

int ui_gcode_viewer_get_max_layer(lv_obj_t* object) {
    auto* viewer = state(object);
    return viewer && viewer->status.layers ? static_cast<int>(viewer->status.layers - 1) : -1;
}

int ui_gcode_viewer_get_layer_count(lv_obj_t* object) {
    auto* viewer = state(object);
    return viewer ? static_cast<int>(viewer->status.layers) : 0;
}

const char* ui_gcode_viewer_get_filename(lv_obj_t* object) {
    auto* viewer = state(object);
    return viewer && !viewer->filename.empty() ? viewer->filename.c_str() : nullptr;
}

float ui_gcode_viewer_get_load_progress(lv_obj_t* object) {
    auto* viewer = state(object);
    return viewer && viewer->phase == GcodeViewerState::Loading ? 0.0f : 1.0f;
}

bool ui_gcode_viewer_pump_offscreen_2d(lv_obj_t* object) {
    auto* viewer = state(object);
    if (!viewer || !ui_gcode_viewer_is_using_2d_mode(object))
        return false;
    wake(*viewer);
    return viewer->embedded_frame != nullptr;
}

std::set<int> ui_gcode_viewer_get_tools_used(lv_obj_t* object) {
    std::set<int> result;
    if (auto* viewer = state(object))
        for (int tool = 0; tool < 4; ++tool)
            if (viewer->status.tools_used & (1u << tool))
                result.insert(tool);
    return result;
}

// The shared native renderer has no object hit testing or host palette overrides.
// Exclusion remains in Helix's explicit object list; preview gestures never issue commands.
void ui_gcode_viewer_set_object_tap_callback(lv_obj_t*, gcode_viewer_object_tap_callback_t, void*) {}
void ui_gcode_viewer_set_object_long_press_callback(lv_obj_t*, gcode_viewer_object_long_press_callback_t, void*) {}
void ui_gcode_viewer_set_highlighted_objects(lv_obj_t*, const std::unordered_set<std::string>&) {}
void ui_gcode_viewer_set_excluded_objects(lv_obj_t*, const std::unordered_set<std::string>&) {}
void ui_gcode_viewer_set_tool_colors(lv_obj_t*, const std::vector<uint32_t>&) {}
bool ui_gcode_viewer_apply_ams_tool_colors(lv_obj_t*) { return false; }
const helix::gcode::ParsedGCodeFile* ui_gcode_viewer_get_parsed_file(lv_obj_t*) { return nullptr; }
bool ui_gcode_viewer_adopt_palette_if_empty(lv_obj_t*, std::vector<std::string>&) { return false; }
const char* ui_gcode_viewer_get_filament_type(lv_obj_t*) { return nullptr; }
void ui_gcode_viewer_set_specular(lv_obj_t*, float, float) {}
void ui_gcode_viewer_set_debug_colors(lv_obj_t*, bool) {}
void ui_gcode_viewer_set_show_travels(lv_obj_t*, bool) {}
void ui_gcode_viewer_set_ghost_mode(lv_obj_t*, int) {}
void ui_gcode_viewer_set_thumbnail_parity(lv_obj_t*, bool) {}
void ui_gcode_viewer_disable_streaming(lv_obj_t*) {}
void ui_gcode_viewer_set_bottom_occluder(lv_obj_t*, lv_obj_t*) {}

static void* xml_create(lv_xml_parser_state_t* parser, const char**) {
    return ui_gcode_viewer_create(static_cast<lv_obj_t*>(lv_xml_state_get_parent(parser)));
}
static void xml_apply(lv_xml_parser_state_t* parser, const char** attributes) {
    lv_xml_obj_apply(parser, attributes);
}
void ui_gcode_viewer_register() {
    static const char* const languages[] = {"zh", nullptr};
    static const char* const tags[] = {"Current layer", "Remaining", nullptr};
    static const char* const translations[] = {"当前层", "剩余"};
    lv_translation_add_static(languages, tags, translations);
    lv_xml_register_widget("gcode_viewer", xml_create, xml_apply);
}
