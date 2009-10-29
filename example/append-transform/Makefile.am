CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: append-transform.so

append-transform.o: append-transform.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o append-transform.o -c append-transform.c

append-transform.so: append-transform.o
	$(CC) $(LDFLAGS) -o append-transform.so append-transform.o

clean:
	rm -f append-transform.so append-transform.o
