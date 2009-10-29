CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: basic-auth-deprecated.so

basic-auth-deprecated.o: basic-auth.deprecated.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o basic-auth-deprecated.o -c basic-auth.deprecated.c

basic-auth-deprecated.so: basic-auth-deprecated.o
	$(CC) $(LDFLAGS) -o basic-auth-deprecated.so basic-auth-deprecated.o

clean:
	rm -f basic-auth-deprecated.so basic-auth-deprecated.o
