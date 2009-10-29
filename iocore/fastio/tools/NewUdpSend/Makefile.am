

OS := $(shell uname -s)
MACHINE := $(shell uname -m)
PLATFORM := $(shell uname -p)


SYSDEFINES = -D$(OS) -DMACHINE=$(MACHINE) -DPLATFORM=$(PLATFORM)

CCFLAGS = $(SYSDEFINES)

ifeq ($(OS), SunOS)
  LIBS = -lnsl -lsocket
endif


CC=gcc

TARGET= UdpSend.$(OS).$(PLATFORM).$(MACHINE)

all : $(TARGET)


$(TARGET): UdpSend.c
	$(CC) UdpSend.c -o $(TARGET) $(LIBS) $(CCFLAGS)


clean:
	rm -f $(TARGET)



