all: tstop.cc
	$(CXX) $^ -o tstop -lcurl -lcurses -g -ltsutil -ltsmgmt -ltsutil -lpthread -Wl,-rpath,/usr/local/lib

clean:
	rm -f tstop
