CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: server-transform.so

server-transform.o: server-transform.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o server-transform.o -c server-transform.c

server-transform.so: server-transform.o
	$(CC) $(LDFLAGS) -o server-transform.so server-transform.o

clean:
	rm -f server-transform.so server-transform.o
