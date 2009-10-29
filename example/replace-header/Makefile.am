CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: replace-header.so

replace-header.o: replace-header.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o replace-header.o -c replace-header.c

replace-header.so: replace-header.o
	$(CC) $(LDFLAGS) -o replace-header.so replace-header.o

clean:
	rm -f replace-header.so replace-header.o
