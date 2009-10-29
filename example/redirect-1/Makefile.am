CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: redirect-1.so

redirect-1.o: redirect-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o redirect-1.o -c redirect-1.c

redirect-1.so: redirect-1.o
	$(CC) $(LDFLAGS) -o redirect-1.so redirect-1.o

clean:
	rm -f redirect-1.so redirect-1.o
