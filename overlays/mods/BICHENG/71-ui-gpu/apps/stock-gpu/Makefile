CC = aarch64-linux-gnu-gcc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -fPIC -fvisibility=hidden -pthread
CPPFLAGS += -DLV_CONF_INCLUDE_SIMPLE -I. -I$(LVGL_DIR) -I../ui-gpu
LDFLAGS += -shared -Wl,-z,defs,-z,relro,-z,now,-Bsymbolic
LDLIBS += $(MALI_LIB) -ldl -lm -pthread
SOURCES = gpu2d.c native_paint.c native_dispatch.c nvg_memory.c ../ui-gpu/gpu_output.c $(LVGL_DIR)/src/libs/nanovg/nanovg.c
HEADERS = $(wildcard *.h) ../ui-gpu/gpu_output.h

all: libstock-gpu.so

libstock-gpu.so: stock_gpu.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) stock_gpu.c $(SOURCES) $(LDLIBS) -o $@

native-gpu-probe.so: ../../test/native_gpu_probe.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(SOURCES) $(LDLIBS) -o $@

.PHONY: all
