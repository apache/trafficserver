CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: session-2.so session-2-dbg.so

session-2.o: session-2.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o session-2.o -c session-2.c

session-2.so: session-2.o
	$(CC) $(LDFLAGS) -o session-2.so session-2.o

session-2-dbg.o: session-2.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o session-2-dbg.o -c session-2.c

session-2-dbg.so: session-2-dbg.o
	$(CC) $(LDFLAGS) -o session-2-dbg.so session-2-dbg.o

clean:
	rm -f session-2.so session-2.o
	rm -f session-2-dbg.so session-2-dbg.o
