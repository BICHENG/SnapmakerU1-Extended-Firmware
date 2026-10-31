CC = aarch64-linux-gnu-gcc
HOSTCC ?= cc
CPPFLAGS ?=
CFLAGS = -O2 -std=c11 -Wall -Wextra -Werror -Wformat=2 -fPIC
SANITIZERS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
MALI_LIB ?=

.PHONY: test all compose check-compose clean
all: libstock-draw-capture.so libnative-resources-probe.so

libstock-draw-capture.so: capture/capture.c capture/capture.h abi/stock_draw_abi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared capture/capture.c -o $@ -ldl

libnative-resources-probe.so: resources/resources_probe.c resources/resources.c resources/resources.h resources/native_types.h resources/vendor_entry.inc abi/image_abi.h abi/stock_draw_abi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wl,-z,relro,-z,now -shared resources/resources_probe.c resources/resources.c -o $@ -ldl

compose:
	@test -f "$(MALI_LIB)" || { echo 'Set MALI_LIB to the matching AArch64 libmali.so'; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -DEGL_NO_X11 -Wl,-z,relro,-z,now -shared compose/compose_probe.c compose/compose_gpu_opaque.c "$(MALI_LIB)" -o libnative-compose-probe.so -ldl

check-compose:
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) -DEGL_NO_X11 -fsyntax-only compose/compose_probe.c compose/compose_gpu_opaque.c

test:
	python3 check_results.py
	$(HOSTCC) $(CFLAGS) $(SANITIZERS) capture/test_capture.c -o capture/test_capture -ldl
	python3 -m unittest discover -s capture -p 'test_*.py' -q
	$(HOSTCC) $(CFLAGS) $(SANITIZERS) resources/test_resources.c -o test-resources
	./test-resources
	$(HOSTCC) $(CFLAGS) $(SANITIZERS) resources/test_comparison.c resources/resources.c -o test-comparison -ldl
	./test-comparison

clean:
	rm -f libstock-draw-capture.so libnative-resources-probe.so libnative-compose-probe.so capture/test_capture test-resources test-comparison
