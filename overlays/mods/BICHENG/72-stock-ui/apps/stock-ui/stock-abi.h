#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *version,*sha256;
    size_t executable_size;
    uintptr_t lv_obj_set_local_style_prop;
    uintptr_t lv_obj_set_pos;
    uintptr_t lv_obj_set_size;
    uintptr_t lv_label_set_text;
    uintptr_t lv_obj_class_create_obj;
    uintptr_t lv_obj_class_init_obj;
    uintptr_t lv_obj_add_event_cb;
    uintptr_t lv_obj_get_child;
    uintptr_t lv_event_get_indev;
    uintptr_t lv_indev_get_point;
    uintptr_t lv_display_get_screen_active;
    uintptr_t lv_display_get_layer_top;
    uintptr_t lv_image_set_src;
    uintptr_t lv_obj_add_flag;
    uintptr_t lv_obj_remove_flag;
    uintptr_t lv_indev_get_read_timer;
    uintptr_t lv_timer_set_period;
    uintptr_t lv_obj_is_visible;
    uintptr_t lv_label_get_text;
    uintptr_t lv_obj_get_style_prop;
    uintptr_t lv_obj_get_child_count;
    uintptr_t lv_image_get_src;
    uintptr_t lv_obj_get_coords;
    uintptr_t lv_lock;
    uintptr_t lv_unlock;
    uintptr_t lv_obj_delete;
    uintptr_t lv_obj_invalidate;
    uintptr_t stock_label_class;
    uintptr_t stock_button_class;
    uintptr_t stock_image_class;
    uintptr_t stock_global_got;
    uintptr_t stock_main_sleep;
    uintptr_t stock_main_short_sleep;
} StockAbi;

static const StockAbi stock_abis[]={
    {.version="2.0.0.205", .executable_size=7010568, .sha256="bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4",
     .lv_obj_set_local_style_prop=0x92ad0,
     .lv_obj_set_pos=0x884b0,
     .lv_obj_set_size=0x888d0,
     .lv_label_set_text=0xe56a0,
     .lv_obj_class_create_obj=0x7f530,
     .lv_obj_class_init_obj=0x7f780,
     .lv_obj_add_event_cb=0x866c0,
     .lv_obj_get_child=0x80030,
     .lv_event_get_indev=0x86bd0,
     .lv_indev_get_point=0x3daa0,
     .lv_display_get_screen_active=0xf8140,
     .lv_display_get_layer_top=0xf8200,
     .lv_image_set_src=0xd1560,
     .lv_obj_add_flag=0x8e930,
     .lv_obj_remove_flag=0x8d240,
     .lv_indev_get_read_timer=0x3dbf0,
     .lv_timer_set_period=0x7a130,
     .lv_obj_is_visible=0x8ac20,
     .lv_label_get_text=0xe4080,
     .lv_obj_get_style_prop=0x913e0,
     .lv_obj_get_child_count=0x803d0,
     .lv_image_get_src=0xd0520,
     .lv_obj_get_coords=0x88e00,
     .lv_lock=0x6cfb0,
     .lv_unlock=0x6cfd0,
     .lv_obj_delete=0x80bc0,
     .lv_obj_invalidate=0x8a420,
     .stock_label_class=0x69e7b8,
     .stock_button_class=0x69e2b8,
     .stock_image_class=0x69e538,
     .stock_global_got=0x69ff40,
     .stock_main_sleep=0x3bb78,
     .stock_main_short_sleep=0x3bb90},
    {.version="2.1.0.157", .executable_size=6563736, .sha256="2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472",
     .lv_obj_set_local_style_prop=0x65800,
     .lv_obj_set_pos=0x5b1f0,
     .lv_obj_set_size=0x5b610,
     .lv_label_set_text=0xb83d0,
     .lv_obj_class_create_obj=0x52270,
     .lv_obj_class_init_obj=0x524c0,
     .lv_obj_add_event_cb=0x59400,
     .lv_obj_get_child=0x52d70,
     .lv_event_get_indev=0x59910,
     .lv_indev_get_point=0x107e0,
     .lv_display_get_screen_active=0xcae70,
     .lv_display_get_layer_top=0xcaf30,
     .lv_image_set_src=0xa4290,
     .lv_obj_add_flag=0x61660,
     .lv_obj_remove_flag=0x5ff80,
     .lv_indev_get_read_timer=0x10930,
     .lv_timer_set_period=0x4ce70,
     .lv_obj_is_visible=0x5d960,
     .lv_label_get_text=0xb6db0,
     .lv_obj_get_style_prop=0x64110,
     .lv_obj_get_child_count=0x53110,
     .lv_image_get_src=0xa3250,
     .lv_obj_get_coords=0x5bb40,
     .lv_lock=0x3fcf0,
     .lv_unlock=0x3fd10,
     .lv_obj_delete=0x53900,
     .lv_obj_invalidate=0x5d160,
     .stock_label_class=0x63f7b0,
     .stock_button_class=0x63f2b0,
     .stock_image_class=0x63f530,
     .stock_global_got=0x640f38,
     .stock_main_sleep=0xe8b8,
     .stock_main_short_sleep=0xe8d0},
};
