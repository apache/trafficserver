CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: append-transform-deprecated.so

append-transform-deprecated.o: append-transform.deprecated.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o append-transform-deprecated.o -c append-transform.deprecated.c

append-transform-deprecated.so: append-transform-deprecated.o
	$(CC) $(LDFLAGS) -o append-transform-deprecated.so append-transform-deprecated.o

clean:
	rm -f append-transform-deprecated.so append-transform-deprecated.o
