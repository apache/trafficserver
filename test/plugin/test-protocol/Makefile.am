CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: test-protocol.so test-protocol-dbg.so

test-protocol.o: test-protocol.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o test-protocol.o -c test-protocol.c

test-protocol.so: test-protocol.o
	$(CC) $(LDFLAGS) -o test-protocol.so test-protocol.o

test-protocol-dbg.o: test-protocol.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o test-protocol-dbg.o -c test-protocol.c

test-protocol-dbg.so: test-protocol-dbg.o
	$(CC) $(LDFLAGS) -o test-protocol-dbg.so test-protocol-dbg.o

clean:
	rm -f test-protocol.so test-protocol.o
	rm -f test-protocol-dbg.so test-protocol-dbg.o
