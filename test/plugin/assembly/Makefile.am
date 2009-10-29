CC=cc
LD=ld
CFLAGS= -g -KPIC -D_REENTRANT=1
LDFLAGS= -G -g

TS_BASE=../../../../..
API_INC= -I. -I$(TS_BASE)/proxy/api/include

default: assembly.so

headers.o: headers.c headers.h common.h
	$(CC) $(CFLAGS) $(API_INC) -o headers.o -c headers.c

list.o: list.c list.h common.h
	$(CC) $(CFLAGS) $(API_INC) -o list.o -c list.c

assembly.o: assembly.c common.h
	$(CC) $(CFLAGS) $(API_INC) -o assembly.o -c assembly.c

assembly.so: assembly.o headers.o list.o
	$(CC) $(LDFLAGS) -o assembly.so assembly.o headers.o list.o

clean:
	rm -f assembly.so assembly.o headers.o list.o
