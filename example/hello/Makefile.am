CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: hello.so

hello.o: hello.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o hello.o -c hello.c

hello.so: hello.o
	$(CC) $(LDFLAGS) -o hello.so hello.o

clean:
	rm -f hello.so hello.o
