#
# Copyright (c) 2006,2008,2011,2017,2018 Solar Designer <solar at openwall.com>
# Copyright (c) 2014,2017 ABC <abc at openwall.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted.
#
# There's ABSOLUTELY NO WARRANTY, express or implied.
#

CC = gcc
LD = $(CC)
RM = rm -f
MKDIR = mkdir -p
CFLAGS = -Wall -O2 -fomit-frame-pointer -D_FILE_OFFSET_BITS=64
LDFLAGS = -s

PROJ = bindex bit
OBJS_COMMON = misc.o buffer.o mime.o encoding.o index.o
OBJS_BINDEX = bindex.o mailbox.o md5/md5.o
OBJS_BIT = bit.o html.o

all: $(PROJ)

check: all tests
tests: test
test:
	$(MAKE) -C tests

bindex: $(OBJS_BINDEX) $(OBJS_COMMON)
	$(LD) $(LDFLAGS) $(OBJS_BINDEX) $(OBJS_COMMON) -o $@

bit: $(OBJS_BIT) $(OBJS_COMMON)
	$(LD) $(LDFLAGS) $(OBJS_BIT) $(OBJS_COMMON) -o $@

bindex.o: mailbox.h
bit.o: html.h
buffer.o: buffer.h
encoding.o: encoding.h buffer.h
html.o: html.h buffer.h encoding.h index.h mime.h misc.h params.h
index.o: index.h misc.h params.h
mailbox.o: mailbox.h buffer.h index.h mime.h misc.h params.h md5/md5.h
mime.o: mime.h buffer.h encoding.h params.h
misc.o: misc.h params.h

md5/md5.o: md5/md5.c md5/md5.h
	$(CC) $(CFLAGS) -c md5/md5.c -o md5/md5.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

clean:
	$(RM) $(PROJ) $(OBJS_BINDEX) $(OBJS_BIT) $(OBJS_COMMON)
	$(MAKE) -C tests clean
