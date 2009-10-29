CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: clusterRPC.so

clusterRPC.o: clusterRPC.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o clusterRPC.o -c clusterRPC.c

clusterRPC.so: clusterRPC.o
	$(CC) $(LDFLAGS) -o clusterRPC.so clusterRPC.o

clean:
	rm -f clusterRPC.so clusterRPC.o
