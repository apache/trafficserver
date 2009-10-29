CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: file-1.so

file-1.o: file-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o file-1.o -c file-1.c

file-1.so: file-1.o
	$(CC) $(LDFLAGS) -o file-1.so file-1.o

clean:
	rm -f file-1.so file-1.o
