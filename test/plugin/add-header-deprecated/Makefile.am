CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: add-header-deprecated.so

add-header-deprecated.o: add-header.deprecated.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o add-header-deprecated.o -c add-header.deprecated.c

add-header-deprecated.so: add-header-deprecated.o
	$(CC) $(LDFLAGS) -o add-header-deprecated.so add-header-deprecated.o

clean:
	rm -f add-header-deprecated.so add-header-deprecated.o
