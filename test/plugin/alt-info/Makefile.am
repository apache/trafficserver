CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: alt-info.so alt-info-dbg.so

alt-info.o: alt-info.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o alt-info.o -c alt-info.c

alt-info.so: alt-info.o
	$(CC) $(LDFLAGS) -o alt-info.so alt-info.o

alt-info-dbg.o: alt-info.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o alt-info-dbg.o -c alt-info.c

alt-info-dbg.so: alt-info-dbg.o
	$(CC) $(LDFLAGS) -o alt-info-dbg.so alt-info-dbg.o

clean:
	rm -f alt-info.so alt-info.o
	rm -f alt-info-dbg.so alt-info-dbg.o
