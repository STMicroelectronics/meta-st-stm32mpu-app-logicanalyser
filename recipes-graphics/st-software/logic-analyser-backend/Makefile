# This Makefile is provided as an example of how to build a process that can
# communicate with a Storyboard application.  It may require modifications in
# order to build on your host system.  Please see ReadMe.html for a complete
# explanation

# Linux users add this
#CFLAGS2 = -Wall $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS2 = -lpthread -lmicrohttpd -lm -lc

LDFLAGS3 = -lpthread

all: backend keyboard

backend: backend.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDFLAGS2)

keyboard: keyboard.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDFLAGS3)
