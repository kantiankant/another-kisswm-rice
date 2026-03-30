CC      = cc
CFLAGS  = -O2 -Wall -Wextra $(shell pkg-config --cflags x11 xft)
LDFLAGS = $(shell pkg-config --libs x11 xft)

all: kantbar

kantbar: kantbar.c
	$(CC) $(CFLAGS) -o kantbar kantbar.c $(LDFLAGS)

install: kantbar
	install -Dm755 kantbar $(DESTDIR)$(PREFIX)/usr/local/bin/kantbar

clean:
	rm -f kantbar

.PHONY: all install clean
