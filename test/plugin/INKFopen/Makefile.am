CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: INKFopen.so

INKFopen.o: INKFopen.c $(API_INC)/InkAPI.h
	$(CC) -g $(CFLAGS) -I$(API_INC) -o INKFopen.o -c INKFopen.c

INKFopen.so: INKFopen.o
	$(CC) $(LDFLAGS) -o INKFopen.so INKFopen.o

clean:
	rm -f INKFopen.so INKFopen.o
