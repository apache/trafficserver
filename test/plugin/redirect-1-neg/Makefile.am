CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: redirect-1-dbg.so

redirect-1-dbg.o: redirect-1-neg.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o redirect-1-dbg.o -c redirect-1-neg.c

redirect-1-dbg.so: redirect-1-dbg.o
	$(CC) $(LDFLAGS) -o redirect-1-dbg.so redirect-1-dbg.o

clean:
	rm -f redirect-1-dbg.so redirect-1-dbg.o
