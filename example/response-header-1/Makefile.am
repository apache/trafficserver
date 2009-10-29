CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: response-header-1.so

response-header-1.o: response-header-1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o response-header-1.o -c response-header-1.c

response-header-1.so: response-header-1.o
	$(CC) $(LDFLAGS) -o response-header-1.so response-header-1.o

clean:
	rm -f response-header-1.so response-header-1.o
