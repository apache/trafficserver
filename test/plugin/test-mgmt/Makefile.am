CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include
OTHER_INC=../../include_other

default: test-mgmt.so

test-mgmt.o: test-mgmt.c $(OTHER_INC)/macro.h $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(OTHER_INC) -I$(API_INC) -o test-mgmt.o -c test-mgmt.c

test-mgmt.so: test-mgmt.o
	$(CC) $(LDFLAGS) -o test-mgmt.so test-mgmt.o

clean:
	rm -f test-mgmt.so test-mgmt.o
