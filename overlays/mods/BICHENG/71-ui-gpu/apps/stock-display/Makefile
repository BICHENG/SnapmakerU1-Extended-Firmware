CC = aarch64-linux-gnu-gcc
HOSTCC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -fPIC
TARGET = libstock-display.so

all: $(TARGET)

$(TARGET): stock-display.o stock-perf.o
	$(CC) -shared $^ -o $@ -ldl

stock-display.o: stock-display.c stock-perf.h
stock-perf.o: stock-perf.c stock-perf.h

test:
	$(HOSTCC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_stock_perf.c -o test-stock-perf
	./test-stock-perf
	$(HOSTCC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_stock_display.c -ldl -o test-stock-display
	./test-stock-display
	rm -f test-stock-perf test-stock-display

clean:
	rm -f $(TARGET) *.o test-stock-perf test-stock-display

.PHONY: all test clean
