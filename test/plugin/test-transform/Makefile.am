CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: test-transform.so test-transform-dbg.so

test-transform.o: test-transform.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o test-transform.o -c test-transform.c

test-transform.so: test-transform.o
	$(CC) $(LDFLAGS) -o test-transform.so test-transform.o

test-transform-dbg.o: test-transform.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o test-transform-dbg.o -c test-transform.c

test-transform-dbg.so: test-transform-dbg.o
	$(CC) $(LDFLAGS) -o test-transform-dbg.so test-transform-dbg.o

clean:
	rm -f test-transform.so test-transform.o
	rm -f test-transform-dbg.so test-transform-dbg.o
