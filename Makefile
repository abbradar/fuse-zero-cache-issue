CC ?= gcc
CFLAGS = -Wall -O2
FUSE_FLAGS = $(shell pkg-config --cflags --libs fuse3) -lrt

ALL = fuse_growtest fuse_growtest_inval fuse_growtest_direct

all: $(ALL)

fuse_growtest: fuse_growtest.c
	$(CC) $(CFLAGS) -o $@ $< $(FUSE_FLAGS)

fuse_growtest_inval: fuse_growtest_inval.c
	$(CC) $(CFLAGS) -o $@ $< $(FUSE_FLAGS)

fuse_growtest_direct: fuse_growtest_direct.c
	$(CC) $(CFLAGS) -o $@ $< $(FUSE_FLAGS)

clean:
	rm -f $(ALL)

.PHONY: all clean
