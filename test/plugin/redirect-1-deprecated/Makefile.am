CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: redirect-1-deprecated.so

redirect-1-deprecated.o: redirect-1.deprecated.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o redirect-1-deprecated.o -c redirect-1.deprecated.c

redirect-1-deprecated.so: redirect-1-deprecated.o
	$(CC) $(LDFLAGS) -o redirect-1-deprecated.so redirect-1-deprecated.o

clean:
	rm -f redirect-1-deprecated.so redirect-1-deprecated.o
