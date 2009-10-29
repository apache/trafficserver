CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: bnull-transform.so

bnull-transform.o: bnull-transform.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o bnull-transform.o -c bnull-transform.c

bnull-transform.so: bnull-transform.o
	$(CC) $(LDFLAGS) -o bnull-transform.so bnull-transform.o

clean:
	rm -f bnull-transform.so bnull-transform.o
