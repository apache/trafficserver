#  This Makefile depends on gmake and having an executable called 
#  'uname -m' to print the architecture name.  If these aren't 
#  available, manually set ARCH, CC, and LIBS and delete or comment 
#  out the machine-dependent section below.
#
#  Output of 'uname -m', 'uname -s', and 'arch':
#
#    uname               -m        -s         arch
#    ------------------------------------------------
#    Solaris sparc      sun4u     SunOS        sun4
#    Solaris x86        i86pc     SunOS       i86pc
#    Linux          i586|i686     Linux   i586|i686
#    FreeBSD             i386   FreeBSD      [none]
#    DEC                alpha      OSF1       alpha
#    Irix (O200)         IP27    IRIX64        irix
#

# begin machine-dependent section
ARCH = $(shell uname -m)

ifeq ($(ARCH),alpha)
CC = gcc
CFLAGS=-g -Wall -fPIC
LDFLAGS=-g -Wall -shared
LD = gcc
endif 

ifeq ($(ARCH),sun4u)
CC=cc
CFLAGS=-g -KPIC -v
LDFLAGS=-G
LD = cc
endif

ifeq ($(ARCH),i86pc)
CC = cc
CFLAGS=-g -KPIC -v
LDFLAGS=-G
LD = cc
endif

ifeq ($(ARCH), i386)
ARCH = freeBSD
CC = gcc
CFLAGS=-g -Wall -fPIC
LDFLAGS=-g -Wall -shared
LD = gcc
endif 

ifeq ($(ARCH),i686)
ARCH = linux
CC = gcc
CFLAGS=-g -Wall -fPIC
LDFLAGS=-g -Wall -shared
LD = gcc
endif 

ifeq ($(ARCH),9000/800)
ARCH = hpux
CC = cc
CFLAGS=+z
LDFLAGS=-b
LD = ld
endif

default: \
	hello_world.so \
	SimSynthServer.so \
	SimSynthServerCache.so \
	LogRequest.so \
	CheckContentServer.so


hello_world.o: hello_world.c ServerAPI.h
	$(CC) $(CFLAGS) -o hello_world.o -c hello_world.c

hello_world.so: hello_world.o
	$(LD) $(LDFLAGS) -o hello_world.so hello_world.o


SimSynthServer.o: SimSynthServer.c ServerAPI.h
	$(CC) $(CFLAGS) -o SimSynthServer.o -c SimSynthServer.c

SimSynthServer.so: SimSynthServer.o
	$(LD) $(LDFLAGS) -o SimSynthServer.so SimSynthServer.o


SimSynthServerCache.o: SimSynthServerCache.c ServerAPI.h
	$(CC) $(CFLAGS) -o SimSynthServerCache.o -c SimSynthServerCache.c

SimSynthServerCache.so: SimSynthServerCache.o
	$(LD) $(LDFLAGS) -o SimSynthServerCache.so SimSynthServerCache.o


LogRequest.o: LogRequest.c ServerAPI.h
	$(CC) $(CFLAGS) -o LogRequest.o -c LogRequest.c

LogRequest.so: LogRequest.o
	$(LD) $(LDFLAGS) -o LogRequest.so LogRequest.o


CheckContentServer.o: CheckContentServer.c ServerAPI.h
	$(CC) $(CFLAGS) -o CheckContentServer.o -c CheckContentServer.c

CheckContentServer.so: CheckContentServer.o
	$(LD) $(LDFLAGS) -o CheckContentServer.so CheckContentServer.o


clean:
	rm -f *.o *.so *~

