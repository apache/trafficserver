CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: INKContSchedule.so

INKContSchedule.o: INKContSchedule.c $(API_INC)/InkAPI.h
	$(CC) -g $(CFLAGS) -I$(API_INC) -o INKContSchedule.o \
		-c INKContSchedule.c

INKContSchedule.so: INKContSchedule.o
	$(CC) $(LDFLAGS) -o INKContSchedule.so INKContSchedule.o

clean:
	rm -f INKContSchedule.so INKContSchedule.o
