CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include
OTHER_INC=../../include_other

default: check-url-0.so check-url-0-dbg.so

check-url-0.o: check-url-0.c $(OTHER_INC)/macro.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(OTHER_INC) -I$(API_INC) -o check-url-0.o -c check-url-0.c

check-url-0.so: check-url-0.o
	$(CC) $(LDFLAGS) -o check-url-0.so check-url-0.o

check-url-0-dbg.o: check-url-0.c $(OTHER_INC)/macro.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(OTHER_INC) -I$(API_INC) -o check-url-0-dbg.o -c check-url-0.c

check-url-0-dbg.so: check-url-0-dbg.o
	$(CC) $(LDFLAGS) -o check-url-0-dbg.so check-url-0-dbg.o

clean:
	rm -f *.so *.o
