#
# Copyright (c) 2006,2008,2011 Solar Designer
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
CFLAGS = -c -Wall -O2 -fomit-frame-pointer -D_FILE_OFFSET_BITS=64
LDFLAGS = -s

PROJ = bindex bit
OBJS_COMMON = misc.o buffer.o mime.o
OBJS_BINDEX = bindex.o mailbox.o md5/md5.o
OBJS_BIT = bit.o html.o

all: $(PROJ)

bindex: $(OBJS_BINDEX) $(OBJS_COMMON)
	$(LD) $(LDFLAGS) $(OBJS_BINDEX) $(OBJS_COMMON) -o $@

bit: $(OBJS_BIT) $(OBJS_COMMON)
	$(LD) $(LDFLAGS) $(OBJS_BIT) $(OBJS_COMMON) -o $@

bindex.o: mailbox.h
bit.o: params.h html.h
buffer.o: buffer.h
mime.o: mime.h buffer.h
html.o: params.h index.h mime.h buffer.h misc.h html.h
mailbox.o: params.h index.h mime.h buffer.h misc.h
misc.o: params.h

md5/md5.o: md5/md5.c md5/md5.h
	$(CC) $(CFLAGS) -c md5/md5.c -o md5/md5.o

.c.o:
	$(CC) $(CFLAGS) $*.c

clean:
	$(RM) $(PROJ) $(OBJS_BINDEX) $(OBJS_BIT) $(OBJS_COMMON)
