CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: null-transform.so

null-transform.o: null-transform.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o null-transform.o -c null-transform.c

null-transform.so: null-transform.o
	$(CC) $(LDFLAGS) -o null-transform.so null-transform.o

clean:
	rm -f null-transform.so null-transform.o
