CC = aarch64-linux-gnu-gcc
HOSTCC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -fPIC
CPPFLAGS += -I../../../../../../common
TARGET = libstock-display.so

all: $(TARGET)

$(TARGET): stock-display.o stock-perf.o ui-display.o
	$(CC) -shared $^ -o $@ -ldl

stock-display.o: stock-display.c stock-perf.h
stock-perf.o: stock-perf.c stock-perf.h
ui-display.o: ../ui-display/ui-display.c ../ui-display/ui-display.h ../../../../../../common/ui-frame.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test:
	$(HOSTCC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_stock_perf.c -o test-stock-perf
	./test-stock-perf
	$(HOSTCC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_stock_display.c ../ui-display/ui-display.c -ldl -o test-stock-display
	./test-stock-display
	$(HOSTCC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_ui_display.c -o test-ui-display
	./test-ui-display
	rm -f test-stock-perf test-stock-display test-ui-display

clean:
	rm -f $(TARGET) *.o test-stock-perf test-stock-display test-ui-display

.PHONY: all test clean
