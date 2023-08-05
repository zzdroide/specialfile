CFLAGS += -std=gnu99 -Wall -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=28
OPT = -O2
SOURCES = diskfile.c main.c

UNAME = $(shell uname)
ifeq ($(UNAME),Darwin)
	CFLAGS += -I/usr/local/include/osxfuse/fuse -D_DARWIN_USE_64_BIT_INODE
	LIBS += -losxfuse -framework CoreFoundation -framework IOKit
	SOURCES += mac-size.c
else ifeq ($(UNAME),FreeBSD)
	CFLAGS += -I/usr/local/include
	LIBS += -L/usr/local/lib -lfuse -lgeom
	SOURCES += fbsd-size.c
else
	LIBS += -lfuse
	SOURCES += linux-size.c
endif

all: specialfile

debug: CFLAGS += -DDEBUG
debug: specialfile

specialfile: $(SOURCES)
	$(CC) $(OPT) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f specialfile

.PHONY: all debug clean
