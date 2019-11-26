# This Makefile is provided as an example of how to build a process that can
# communicate with a Storyboard application.  It may require modifications in
# order to build on your host system.  Please see ReadMe.html for a complete
# explanation

# Linux users add this
LDFLAGS_ORI = $(LDFLAGS)

CFLAGS += -Wall $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS += -lpthread -lmicrohttpd -lm -lc

LDFLAGS2 = $(LDFLAGS_ORI) $(shell pkg-config --libs gtk+-3.0)
LDFLAGS2 += -lpthread -lm -lc


all: backend backend_gtk keyboard

backend: backend.c
	$(CC) -o $@ $^ $(LDFLAGS)

backend_gtk: backend.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS2)

keyboard: keyboard.c
	$(CC) -o $@ $^ $(LDFLAGS)
