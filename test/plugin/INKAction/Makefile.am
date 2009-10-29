
#CPLUSPLUS=g++
CPLUSPLUS=CC

CPP=@CPP@
CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include
LIBINK_INC=../../../../../ts-libinktomi

default: INKAction.so

INKAction.so: INKAction.o
	$(CPLUSPLUS) $(LDFLAGS) -g -o INKAction.so INKAction.o

INKAction.o: INKAction.cc $(API_INC)/InkAPI.h $(LIBINK_INC)/ink_assert.h
	$(CPLUSPLUS) $(CFLAGS) -g -I$(API_INC) -o INKAction.o \
		-I$(LIBINK_INC) -c INKAction.cc

clean:
	rm -f   INKAction.o	INKAction.so

