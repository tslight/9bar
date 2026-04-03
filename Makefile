PREFIX = /usr/local
CFLAGS	 ?= -O2 -pedantic -std=c11
CFLAGS	 += -Wall -Wconversion -Wextra -Wshadow -Wunused
CFLAGS	 += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS	 += -Wuninitialized -Wimplicit-fallthrough
CFLAGS	 += `pkg-config --cflags x11 xft`
LDFLAGS   = `pkg-config --libs x11 xft`

9bar: 9bar.c
	$(CC) $(CFLAGS) -o $@ 9bar.c $(LDFLAGS)

clean:
	rm -f 9bar

install: 9bar
	install -s 9bar $(PREFIX)/bin/

.PHONY: clean install