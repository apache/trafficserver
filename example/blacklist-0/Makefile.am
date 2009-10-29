CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: blacklist-0.so

blacklist-0.o: blacklist-0.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o blacklist-0.o -c blacklist-0.c

blacklist-0.so: blacklist-0.o
	$(CC) $(LDFLAGS) -o blacklist-0.so blacklist-0.o

clean:
	rm -f blacklist-0.so blacklist-0.o
