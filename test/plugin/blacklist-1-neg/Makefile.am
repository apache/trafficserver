CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: blacklist-1-dbg.so

blacklist-1-dbg.o: blacklist-1-neg.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o blacklist-1-dbg.o -c blacklist-1-neg.c

blacklist-1-dbg.so: blacklist-1-dbg.o
	$(CC) $(LDFLAGS) -o blacklist-1-dbg.so blacklist-1-dbg.o

clean:
	rm -f blacklist-1-dbg.so blacklist-1-dbg.o
