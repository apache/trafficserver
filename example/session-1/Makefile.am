CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: session-1.so

session-1.o: session-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o session-1.o -c session-1.c

session-1.so: session-1.o
	$(CC) $(LDFLAGS) -o session-1.so session-1.o

clean:
	rm -f session-1.so session-1.o
