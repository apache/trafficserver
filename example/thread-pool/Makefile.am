CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: psi.so

thread.o: thread.c thread.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o thread.o -c thread.c

psi.o: psi.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o psi.o -c psi.c

psi.so: psi.o thread.o
	$(CC) $(LDFLAGS) -o psi.so psi.o thread.o

clean:
	rm -f psi.so psi.o thread.o

