CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += $(shell pkg-config --cflags libpng)
CPPFLAGS += -Iserver/lib
LDLIBS += $(shell pkg-config --libs libpng)

.PHONY: all clean test

all: printer-server escpos-print escpos-capabilities

printer-server: server/backend.c server/lib/escpos.c server/lib/escpos.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ server/backend.c server/lib/escpos.c $(LDLIBS)

escpos-print: server/escpos-print.c server/lib/escpos.c server/lib/escpos.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ server/escpos-print.c server/lib/escpos.c $(LDLIBS)

escpos-capabilities: server/escpos-capabilities.c server/lib/escpos.c server/lib/escpos.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ server/escpos-capabilities.c server/lib/escpos.c $(LDLIBS)

escpos-test: server/tests/escpos-test.c server/lib/escpos.c server/lib/escpos.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ server/tests/escpos-test.c server/lib/escpos.c $(LDLIBS)

test: escpos-test
	./escpos-test

clean:
	rm -f printer-server escpos-print escpos-capabilities escpos-test
