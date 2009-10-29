CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: add-header.so

add-header.o: add-header.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o add-header.o -c add-header.c

add-header.so: add-header.o
	$(CC) $(LDFLAGS) -o add-header.so add-header.o

clean:
	rm -f add-header.so add-header.o
