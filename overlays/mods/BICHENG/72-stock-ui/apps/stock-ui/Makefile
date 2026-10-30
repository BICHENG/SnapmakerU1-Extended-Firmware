CC ?= aarch64-linux-gnu-gcc
HOSTCC ?= cc
CFLAGS = -O2 -Wall -Wextra -Werror -fPIC
MALI_LIB ?= /usr/lib/libmali.so.1
TARGET = libstock-ui.so
all: $(TARGET)
$(TARGET): stock-ui.c gcode.c gpu.c gcode.h gpu.h stock-abi.h
	$(CC) $(CFLAGS) -shared stock-ui.c gcode.c gpu.c $(MALI_LIB) -o $@ -ldl -lm -pthread
test:
	$(HOSTCC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror gcode.c test_gcode.c -lm -o test-gcode
	./test-gcode
	rm -f test-gcode
clean:
	rm -f $(TARGET) test-gcode
.PHONY: all test clean
