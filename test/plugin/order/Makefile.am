CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: orderstartplugin.so orderplugin1.so orderplugin2.so orderplugin3.so orderplugin4.so


orderstartplugin.o: orderstartplugin.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o orderstartplugin.o -c orderstartplugin.c
orderplugin1.o: orderplugin1.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o orderplugin1.o -c orderplugin1.c
orderplugin2.o: orderplugin2.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o orderplugin2.o -c orderplugin2.c
orderplugin3.o: orderplugin3.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o orderplugin3.o -c orderplugin3.c
orderplugin4.o: orderplugin4.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o orderplugin4.o -c orderplugin4.c


orderstartplugin.so: orderstartplugin.o
	$(LD) $(LDFLAGS) -o orderstartplugin.so orderstartplugin.o
orderplugin1.so: orderplugin1.o
	$(LD) $(LDFLAGS) -o orderplugin1.so orderplugin1.o
orderplugin2.so: orderplugin2.o
	$(LD) $(LDFLAGS) -o orderplugin2.so orderplugin2.o
orderplugin3.so: orderplugin3.o
	$(LD) $(LDFLAGS) -o orderplugin3.so orderplugin3.o
orderplugin4.so: orderplugin4.o
	$(LD) $(LDFLAGS) -o orderplugin4.so orderplugin4.o

clean:
	rm -f orderstartplugin.so orderstartplugin.o
	rm -f orderplugin1.so orderplugin1.o
	rm -f orderplugin2.so orderplugin2.o
	rm -f orderplugin3.so orderplugin3.o
	rm -f orderplugin4.so orderplugin4.o
