#
# Makefile for userland FastIO benchmark
#
#


CC = gcc

SRC = udpbmark.c requtil.c

ARCH := $(shell uname -m)
INST := $(shell uname -p)
SYS  := $(shell uname -s)

OBJS = $(SRC:%.c=$(INST)/%.o)

FIOLIB = ../../libfastIO/$(INST)/libfastio.o
INCLUDES = -I../../libfastIO -I../../include -I..

SRC_ROOT = .

TARGET = $(INST)/udpbmark

all: $(TARGET)


# basics

CFLAGS += $(OPT) -g -D$(SYS) #-DSIMULATE_COPY 
OS = $(shell uname -s)


LDFLAGS += -lnsl -lsocket -lcurses -lpthread

$(TARGET): $(OBJS) $(FIOLIB)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(FIOLIB) $(LDFLAGS)


$(INST)/%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean: 
	rm -rf $(TARGET) $(OBJS)
