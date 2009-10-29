CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@ -lz
API_INC=../../include

default: gzip.so gunzip.so

gzip.o: gzip.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -I/usr/local/include -o gzip.o -c gzip.c

gunzip.o: gunzip.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -I/usr/local/include -o gunzip.o -c gunzip.c

gzip.so: gzip.o
	$(CC) $(LDFLAGS) -L/usr/local/lib -o gzip.so gzip.o

gunzip.so: gunzip.o
	$(CC) $(LDFLAGS) -L/usr/local/lib -o gunzip.so gunzip.o

clean:
	rm -f gzip.so gzip.o gunzip.so gunzip.o 
