CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: write-server-ip.so write-server-ip-dbg.so

write-server-ip.o: write-server-ip.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o write-server-ip.o -c write-server-ip.c

write-server-ip.so: write-server-ip.o
	$(CC) $(LDFLAGS) -o write-server-ip.so write-server-ip.o

write-server-ip-dbg.o: write-server-ip.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -DDEBUG -I$(API_INC) -o write-server-ip-dbg.o -c write-server-ip.c

write-server-ip-dbg.so: write-server-ip-dbg.o
	$(CC) $(LDFLAGS) -o write-server-ip-dbg.so write-server-ip-dbg.o

clean:
	rm -f write-server-ip.so write-server-ip.o
	rm -f write-server-ip-dbg.so write-server-ip-dbg.o
