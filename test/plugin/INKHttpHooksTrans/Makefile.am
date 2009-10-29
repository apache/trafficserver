
#CPLUSPLUS=g++
CPLUSPLUS=CC

CPP=@CPP@
CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include
LIBINK_INC=../../../../../ts-libinktomi

default: INKHttpHook.so INKHttpSsnHookAdd.so INKHttpSelAlt.so \
	INKHttpTransaction.so INKHttpTxnErrBodySet.so


########## INKHttpHook.so 
INKHttpHook.o: INKHttpHook.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpHook.o -c INKHttpHook.c
INKHttpHook.so: INKHttpHook.o
	$(CC) $(LDFLAGS) -g -o INKHttpHook.so INKHttpHook.o

########## INKHttpSsnHookAdd.so 
INKHttpSsnHookAdd.o: INKHttpSsnHookAdd.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpSsnHookAdd.o -c \
		INKHttpSsnHookAdd.c
INKHttpSsnHookAdd.so: INKHttpSsnHookAdd.o
	$(CC) $(LDFLAGS) -g -o INKHttpSsnHookAdd.so INKHttpSsnHookAdd.o

INKHttpSelAlt.so: INKHttpSelAlt.o 
	$(CC) $(LDFLAGS) -g -o INKHttpSelAlt.so INKHttpSelAlt.o
INKHttpSelAlt.o: INKHttpSelAlt.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpSelAlt.o -c INKHttpSelAlt.c

INKHttpTransaction.so: INKHttpTransaction.o 
	$(CC) $(LDFLAGS) -g -o INKHttpTransaction.so INKHttpTransaction.o
INKHttpTransaction.o: INKHttpTransaction.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpTransaction.o -c INKHttpTransaction.c

INKHttpTxnErrBodySet.so: INKHttpTxnErrBodySet.o
	$(CC) $(LDFLAGS) -g -o INKHttpTxnErrBodySet.so INKHttpTxnErrBodySet.o
	
INKHttpTxnErrBodySet.o: INKHttpTxnErrBodySet.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -g -I$(API_INC) -o INKHttpTxnErrBodySet.o \
		-c INKHttpTxnErrBodySet.c

INKHttpTxnIPAddress.so: INKHttpTxnIPAddress.o
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKHttpTxnIPAddress.so INKHttpTxnIPAddress.o
INKHttpTxnIPAddress.o: INKHttpTxnIPAddress.cc $(API_INC)/InkAPI.h \
			$(LIBINK_INC)/ink_assert.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) -o INKHttpTxnIPAddress.o \
		-I$(LIBINK_INC) -c INKHttpTxnIPAddress.cc

clean:
	rm -f   INKHttpHook.o		INKHttpHook.so  \
		INKHttpSsnHookAdd.o	INKHttpSsnHookAdd.so \
		INKHttpSelAlt.o		INKHttpSelAlt.so \
		INKHttpTransaction.o	INKHttpTransaction.so \
		INKHttpTxnErrBodySet.o  INKHttpTxnErrBodySet.so \
		INKHttpTxnIPAddress.o	INKHttpTxnIPAddress.so
