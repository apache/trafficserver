CC=cc
LD=ld
CFLAGS= -g -D_REENTRANT=1 -m32
LDFLAGS= -g -shared -m32

TS_BASE=../../../..
API_INC= -I. -I$(TS_BASE)/proxy/api/include

default: cache_plugin.so

cache_plugin.o: cache_plugin.cc
	$(CXX) $(CFLAGS) $(API_INC) -o cache_plugin.o -c cache_plugin.cc

cache_plugin.so: cache_plugin.o
	$(CXX) $(LDFLAGS) -o cache_plugin.so cache_plugin.o

clean:
	rm -f cache_plugin.so cache_plugin.o
