# Generated automatically from Makefile.in by configure.
CC=cc
LD=ld
CFLAGS= -KPIC -g
# Add flag -g to compile in debug
# Add flag -DLOG_ROLL to enable plugin manual log rolling
LDFLAGS= -G
API_INC=-I../../../include 

default: climate.so

climate.o: climate.c events.h
	$(CC) $(CFLAGS) $(API_INC) -o climate.o -c climate.c

climate.so: climate.o
	$(CC) $(LDFLAGS) -o climate.so climate.o 

clean:
	rm -f climate.so climate.o
