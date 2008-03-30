CC = gcc
LD = $(CC)
RM = rm -f
MKDIR = mkdir -p
CFLAGS = -c -Wall -O2 -fomit-frame-pointer
LDFLAGS = -s

PROJ = bindex bit
OBJS_COMMON = misc.o
OBJS_BINDEX = bindex.o mailbox.o
OBJS_BIT = bit.o html.o

all: $(PROJ)

bindex: $(OBJS_BINDEX) $(OBJS_COMMON)
	$(LD) $(LDFLAGS) $(OBJS_BINDEX) $(OBJS_COMMON) -o $@

bit: $(OBJS_BIT) $(OBJS_COMMON)
	$(LD) $(LDFLAGS) $(OBJS_BIT) $(OBJS_COMMON) -o $@

bindex.o: mailbox.h
bit.o: params.h html.h
html.o: params.h index.h misc.h
mailbox.o: params.h index.h misc.h
misc.o: params.h

.c.o:
	$(CC) $(CFLAGS) $*.c

clean:
	$(RM) $(PROJ) $(OBJS_BINDEX) $(OBJS_BIT) $(OBJS_COMMON)
