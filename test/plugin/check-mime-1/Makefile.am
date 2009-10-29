CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: check-mime-1.so check-mime-1-dbg.so

check-mime-1.o: check-mime-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o check-mime-1.o -c check-mime-1.c

check-mime-1.so: check-mime-1.o
	$(CC) $(LDFLAGS) -o check-mime-1.so check-mime-1.o

check-mime-1-dbg.o: check-mime-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o check-mime-1-dbg.o -c check-mime-1.c

check-mime-1-dbg.so: check-mime-1-dbg.o
	$(CC) $(LDFLAGS) -o check-mime-1-dbg.so check-mime-1-dbg.o

clean:
	rm -f check-mime-1.so check-mime-1.o
	rm -f check-mime-1-dbg.so check-mime-1-dbg.o
