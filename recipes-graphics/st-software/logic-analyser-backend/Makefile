# This Makefile is provided as an example of how to build a process that can
# communicate with a Storyboard application.  It may require modifications in
# order to build on your host system.  Please see ReadMe.html for a complete
# explanation

# Linux users add this
CFLAGS+= -DGRE_TARGET_OS_linux
LDFLAGS += -lpthread -lmicrohttpd -lm -lc

all: backend keyboard

backend: backend.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

keyboard: keyboard.c
	$(CC) -o $@ $^ $(LDFLAGS)
