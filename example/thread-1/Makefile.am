CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: thread-1.so

thread-1.o: thread-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o thread-1.o -c thread-1.c

thread-1.so: thread-1.o
	$(CC) $(LDFLAGS) -o thread-1.so thread-1.o

clean:
	rm -f thread-1.so thread-1.o
