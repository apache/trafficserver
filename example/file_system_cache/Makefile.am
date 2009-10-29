LDFLAGS= -g -m32 -shared
TS_BASE=../../../..
CXXFLAGS= -g -D_REENTRANT=1 -m32 -I. -I$(TS_BASE)/proxy/api/include

diskcache_plugin.so: diskcache_plugin.o DiskCache.o
	$(CXX) $(LDFLAGS) $^ -o $@

test_diskcache: DiskCache.o test_diskcache.o
	$(CXX) -g -m32 $^ -o $@ -lcrypto -pthread -lrt




clean:
	rm -f *.o test_diskcache

