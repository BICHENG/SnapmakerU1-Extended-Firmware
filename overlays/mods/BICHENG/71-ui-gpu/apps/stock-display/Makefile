CC = aarch64-linux-gnu-gcc
HOSTCC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -fPIC -pthread
CPPFLAGS += -I../../../../../../common
TARGET = libstock-display.so

all: $(TARGET)

$(TARGET): stock-display.o stock-perf.o stock-release.o ui-display.o
	$(CC) -shared $^ -o $@ -ldl -pthread

stock-display.o: stock-display.c stock-perf.h stock-release.h
stock-perf.o: stock-perf.c stock-perf.h
stock-release.o: stock-release.c stock-release.h
ui-display.o: ../ui-display/ui-display.c ../ui-display/ui-display.h ../../../../../../common/ui-frame.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test:
	$(HOSTCC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_stock_release.c -o test-stock-release
	./test-stock-release
	$(HOSTCC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_stock_perf.c -o test-stock-perf
	./test-stock-perf
	$(HOSTCC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_stock_display.c ../ui-display/ui-display.c -ldl -pthread -o test-stock-display
	./test-stock-display
	$(HOSTCC) $(CPPFLAGS) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror ../../test/test_ui_display.c -pthread -o test-ui-display
	./test-ui-display
	rm -f test-stock-perf test-stock-display test-ui-display test-stock-release

clean:
	rm -f $(TARGET) *.o test-stock-perf test-stock-display test-ui-display test-stock-release

.PHONY: all test clean
