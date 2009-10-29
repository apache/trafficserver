CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: blacklist-1.so

blacklist-1.o: blacklist-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o blacklist-1.o -c blacklist-1.c

blacklist-1.so: blacklist-1.o
	$(CC) $(LDFLAGS) -o blacklist-1.so blacklist-1.o

clean:
	rm -f blacklist-1.so blacklist-1.o
