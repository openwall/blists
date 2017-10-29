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
CFLAGS  = -c -Wall -O2 -fomit-frame-pointer -D_FILE_OFFSET_BITS=64
LDFLAGS = -s

PROJ = tests
OBJS_COMMON = tests.o

all: $(PROJ) run

run: $(PROJ)
	./tests

tests: $(OBJS_COMMON)
	$(LD) $(LDFLAGS) $(OBJS_COMMON) -o $@

tests.o: ../*.c ../*.h

.c.o:
	$(CC) $(CFLAGS) $*.c

clean:
	$(RM) $(PROJ) $(OBJS_COMMON)
