# Makefile for SDKtest_client Examples.
#
#
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
OS_TYPE = $(shell uname -s)

ifeq ($(ARCH),alpha)
CC = gcc
CFLAGS=-g -Wall -fPIC
LDFLAGS= -shared
LD = gcc
endif

ifeq ($(ARCH),sun4u)
ARCH = sun4
CC=cc
CFLAGS=-g -KPIC -v
LDFLAGS=-G
LD=cc
endif

#ifeq ($(ARCH),i86pc)
#CC = cc
#CFLAGS=-g -KPIC -v
#LDFLAGS=-G
#LD = cc
#endif

ifeq ($(ARCH), i86pc)
ARCH = freeBSD
CC = gcc
CFLAGS=-g -Wall -fPIC
LDFLAGS=-g -Wall -shared
LD = gcc
endif 

ifeq ($(ARCH), i386)
ARCH = freeBSD
CC = gcc
CFLAGS=-g -Wall -fPIC
LDFLAGS=-g -Wall -shared
LD = gcc
endif 

ifeq ($(ARCH),IP27)
ARCH = irix
CC = cc
CFLAGS=-g -KPIC -v
LDFLAGS=-G
LD = cc
endif

ifeq ($(ARCH),i686)
ARCH = linux
CC = gcc
CFLAGS=-g -Wall -fPIC
LDFLAGS=-g -Wall -shared
LD = gcc
endif 

ifeq ($(OS_TYPE),HP-UX)
CC = cc
CFLAGS = +z
LDFLAGS = -b
LD = ld
endif

default: \
	hello_world.so \
	SimSDKtest.so \
	SimSDKtestCache.so \
    BlackList.so \
	CheckContentClient.so \
	SimBasicAuth-0.so \
	SimBasicAuth-1.so


hello_world.o: hello_world.c ClientAPI.h
	$(CC) $(CFLAGS) -o hello_world.o -c hello_world.c

hello_world.so: hello_world.o
	$(LD) $(LDFLAGS) -o hello_world.so hello_world.o

SimSDKtest.o: SimSDKtest.c ClientAPI.h
	$(CC) $(CFLAGS) -o SimSDKtest.o -c SimSDKtest.c

SimSDKtest.so: SimSDKtest.o
	$(LD) $(LDFLAGS) -o SimSDKtest.so SimSDKtest.o


SimSDKtestCache.o: SimSDKtestCache.c ClientAPI.h
	$(CC) $(CFLAGS) -o SimSDKtestCache.o -c SimSDKtestCache.c

SimSDKtestCache.so: SimSDKtestCache.o
	$(LD) $(LDFLAGS) -o SimSDKtestCache.so SimSDKtestCache.o


BlackList.o: BlackList.c ClientAPI.h
	$(CC) $(CFLAGS) -o BlackList.o -c BlackList.c

BlackList.so: BlackList.o
	$(LD) $(LDFLAGS) -o BlackList.so BlackList.o

CheckContentClient.o: CheckContentClient.c ClientAPI.h
	$(CC) $(CFLAGS) -o CheckContentClient.o -c CheckContentClient.c

CheckContentClient.so: CheckContentClient.o
	$(LD) $(LDFLAGS) -o CheckContentClient.so CheckContentClient.o

SimBasicAuth-0.o: SimBasicAuth-0.c ClientAPI.h
	$(CC) $(CFLAGS) -o SimBasicAuth-0.o -c SimBasicAuth-0.c

SimBasicAuth-0.so: SimBasicAuth-0.o
	$(LD) $(LDFLAGS) -o SimBasicAuth-0.so SimBasicAuth-0.o

SimBasicAuth-1.o: SimBasicAuth-1.c ClientAPI.h
	$(CC) $(CFLAGS) -o SimBasicAuth-1.o -c SimBasicAuth-1.c

SimBasicAuth-1.so: SimBasicAuth-1.o
	$(LD) $(LDFLAGS) -o SimBasicAuth-1.so SimBasicAuth-1.o
clean:
	rm -f *.o *.so *~

