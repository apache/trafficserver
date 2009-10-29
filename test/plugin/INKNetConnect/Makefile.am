CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: INKNetConnectTest.so INKqa8418.so

INKNetConnectTest.o: INKNetConnectTest.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o INKNetConnectTest.o -c INKNetConnectTest.c

INKNetConnectTest.so: INKNetConnectTest.o
	$(CC) $(LDFLAGS) -o INKNetConnectTest.so INKNetConnectTest.o

INKqa8418.o: INKqa8418.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o INKqa8418.o -c INKqa8418.c

INKqa8418.so: INKqa8418.o
	$(CC) $(LDFLAGS) -o INKqa8418.so INKqa8418.o

clean:
	rm -f *.so *.o
