CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: check-constants.so

check-constants.o: check-constants.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o check-constants.o -c check-constants.c

check-constants.so: check-constants.o
	$(CC) $(LDFLAGS) -o check-constants.so check-constants.o

clean:
	rm -f check-constants.so check-constants.o
