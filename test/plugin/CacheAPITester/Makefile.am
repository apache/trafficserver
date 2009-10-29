CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: CacheAPITester.so CacheAPITester-dbg.so

CacheAPITester.o: CacheAPITester.c CacheAPITester.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o CacheAPITester.o -c CacheAPITester.c

CacheAPITester.so: CacheAPITester.o
	$(CC) $(LDFLAGS) -o CacheAPITester.so CacheAPITester.o

CacheAPITester-dbg.o: CacheAPITester.c CacheAPITester.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o CacheAPITester-dbg.o -c CacheAPITester.c

CacheAPITester-dbg.so: CacheAPITester-dbg.o
	$(CC) $(LDFLAGS) -DDEBUG -o CacheAPITester-dbg.so CacheAPITester-dbg.o

clean:
	rm -f CacheAPITester.so CacheAPITester.o
	rm -f CacheAPITester-dbg.so CacheAPITester-dbg.o

