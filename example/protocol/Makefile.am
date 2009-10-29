CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../include

default: protocol.so

TxnSM.o: TxnSM.c TxnSM.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o TxnSM.o -c TxnSM.c

Protocol.o: Protocol.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o Protocol.o -c Protocol.c

protocol.so: Protocol.o TxnSM.o
	$(CC) $(LDFLAGS) -o protocol.so Protocol.o TxnSM.o

clean:
	rm -f protocol.so Protocol.o TxnSM.o

