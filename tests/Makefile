#
# Copyright (c) 2017 ABC <abc at openwall.com>
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
CFLAGS  = -ggdb -c -Wall -D_FILE_OFFSET_BITS=64
LDFLAGS = -ggdb

PROJ = tests
OBJS_COMMON = tests.o

all: $(PROJ) run

run: $(PROJ)
	./tests

tests: $(OBJS_COMMON) ../*.c ../*.h
	$(LD) $(LDFLAGS) $(OBJS_COMMON) -o $@


md5.o: ../md5/md5.c ../md5/md5.h
	$(CC) $(CFLAGS) -c ../md5/md5.c -o md5.o

.c.o:
	$(CC) $(CFLAGS) $*.c

clean:
	$(RM) $(PROJ) $(OBJS_COMMON)
