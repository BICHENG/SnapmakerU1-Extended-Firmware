CC = aarch64-linux-gnu-gcc
CFLAGS = -O2 -Wall -Wextra -Werror -fPIC
PREVIEW_DIR ?= ../../../71-ui-gpu/apps/gcode-preview
override CPPFLAGS += -I$(PREVIEW_DIR)
TARGET = libstock-ui.so

all: $(TARGET)
$(TARGET): stock-ui.o
	$(CC) -shared $^ -L$(PREVIEW_DIR) -lgcode-preview -Wl,-rpath,/usr/local/lib -o $@ -ldl
stock-ui.o: stock-ui.c stock-abi.h $(PREVIEW_DIR)/gcode-preview.h
clean:
	rm -f $(TARGET) *.o
.PHONY: all clean
