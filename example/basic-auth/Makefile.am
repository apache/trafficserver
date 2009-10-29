CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: basic-auth.so

basic-auth.o: basic-auth.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o basic-auth.o -c basic-auth.c

basic-auth.so: basic-auth.o
	$(CC) $(LDFLAGS) -o basic-auth.so basic-auth.o

clean:
	rm -f basic-auth.so basic-auth.o
