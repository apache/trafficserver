CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: lookup.so lookup-dbg.so

lookup.o: lookup.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o lookup.o -c lookup.c

lookup.so: lookup.o
	$(CC) $(LDFLAGS) -o lookup.so lookup.o

lookup-dbg.o: lookup.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -DDEBUG -o lookup-dbg.o -c lookup.c

lookup-dbg.so: lookup-dbg.o
	$(CC) $(LDFLAGS) -o lookup-dbg.so lookup-dbg.o

clean:
	rm -f lookup.so lookup.o lookup-dbg.o lookup-dbg.so
