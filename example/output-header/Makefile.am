CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: output-header.so

output-header.o: output-header.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o output-header.o -c output-header.c

output-header.so: output-header.o
	$(CC) $(LDFLAGS) -o output-header.so output-header.o

clean:
	rm -f output-header.so output-header.o
