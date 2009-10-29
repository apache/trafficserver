CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include
REMAP_API_INC=../../..

default: remap.so

remap.o: replace-header.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(REMAP_API_INC) -I$(API_INC) -o remap.o -c remap.c

remap.so: remap.o
	$(CC) $(LDFLAGS) -o remap.so remap.o

clean:
	rm -f remap.so remap.o
