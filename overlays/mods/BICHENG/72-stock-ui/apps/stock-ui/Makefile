CC ?= aarch64-linux-gnu-gcc
CXX ?= aarch64-linux-gnu-g++
HOSTCC ?= cc
HOSTCXX ?= c++
CFLAGS = -O2 -Wall -Wextra -Werror -fPIC
CXXFLAGS = $(CFLAGS) -std=c++17
MALI_LIB ?= /usr/lib/libmali.so.1
PROGRESS_LIBS ?= -lboost_json -lcurl
GPU_OUTPUT_DIR ?= ../../../71-ui-gpu/apps/ui-gpu
override CPPFLAGS += -I$(GPU_OUTPUT_DIR)
TARGET = libstock-ui.so
all: $(TARGET)
$(TARGET): stock-ui.o gcode.o gpu.o gpu-output.o progress.o
	$(CXX) -shared $^ $(MALI_LIB) $(PROGRESS_LIBS) -o $@ -ldl -lm -pthread
stock-ui.o: stock-ui.c gpu.h gcode.h progress.h stock-abi.h
gcode.o: gcode.c gcode.h
gpu.o: gpu.c gpu.h gcode.h $(GPU_OUTPUT_DIR)/gpu_output.h
gpu-output.o: $(GPU_OUTPUT_DIR)/gpu_output.c $(GPU_OUTPUT_DIR)/gpu_output.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@
progress.o: progress.cpp progress.h
gpu-profile.o: gpu.c gpu.h gcode.h $(GPU_OUTPUT_DIR)/gpu_output.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSTOCK_GPU_PROFILE -c gpu.c -o $@
bench-gpu: bench_gpu.c gpu-profile.o gpu-output.o gcode.o
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSTOCK_GPU_PROFILE $^ $(MALI_LIB) -lm -o $@
test:
	$(HOSTCC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror gcode.c test_gcode.c -lm -o test-gcode
	./test-gcode
	$(HOSTCXX) -std=c++17 -O0 -g -fsanitize=address,undefined -Wall -Wextra -Werror progress.cpp test_progress.cpp -lboost_json -lcurl -o test-progress
	./test-progress
	rm -f test-gcode test-progress
clean:
	rm -f $(TARGET) *.o test-gcode test-progress bench-gpu
.PHONY: all test clean
