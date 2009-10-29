CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../../include

default: net_bridge.so \
	net_bridge-dbg.so \
	serve_file.so \
	serve_file-dbg.so \
	http_connect_bridge.so \
	http_connect_bridge-dbg.so \
	test_dealloc.so

net_bridge.o : net_bridge.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o net_bridge.o -c net_bridge.c

net_bridge.so: net_bridge.o
	$(CC) $(LDFLAGS) -o net_bridge.so net_bridge.o

net_bridge-dbg.o : net_bridge.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -DDEBUG -o net_bridge-dbg.o -c net_bridge.c

net_bridge-dbg.so: net_bridge-dbg.o
	$(CC) $(LDFLAGS) -o net_bridge-dbg.so net_bridge-dbg.o

serve_file.o : serve_file.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o serve_file.o -c serve_file.c

serve_file.so: serve_file.o
	$(CC) $(LDFLAGS) -o serve_file.so serve_file.o

serve_file-dbg.o : serve_file.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -DDEBUG -o serve_file-dbg.o -c serve_file.c

serve_file-dbg.so: serve_file-dbg.o
	$(CC) $(LDFLAGS) -o serve_file-dbg.so serve_file-dbg.o

http_connect_bridge.o : http_connect_bridge.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o http_connect_bridge.o -c http_connect_bridge.c

http_connect_bridge.so: http_connect_bridge.o
	$(CC) $(LDFLAGS) -o http_connect_bridge.so http_connect_bridge.o

http_connect_bridge-dbg.o : http_connect_bridge.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -DDEBUG -o http_connect_bridge-dbg.o -c http_connect_bridge.c

http_connect_bridge-dbg.so: http_connect_bridge-dbg.o
	$(CC) $(LDFLAGS) -o http_connect_bridge-dbg.so http_connect_bridge-dbg.o

test_dealloc.o : test_dealloc.c $(API_INC)/InkAPI.h
	$(CC) $(CFLAGS) -I$(API_INC) -o test_dealloc.o -c test_dealloc.c

test_dealloc.so: test_dealloc.o
	$(CC) $(LDFLAGS) -o test_dealloc.so test_dealloc.o


clean:
	rm -f net_bridge.o net_bridge.so net_bridge-dbg.o net_bridge-dbg.so \
	sever_file.so serve_file.o sever_file-dbg.so serve_file-dbg.o \
	test_dealloc.so test_dealloc.o \
	http_connect_bridge.so http_connect_bridge.o http_connect_bridge-dbg.so http_connect_bridge-dbg.o
