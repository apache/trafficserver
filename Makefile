all: slice 

slice_la_SOURCES = \
	ContentRange.cc \
	HttpHeader.cc \
	intercept.cc \
	range.cc \
	slice.cc \

slice_la_HEADERS = \
	ContentRange.h \
	HttpHeader.h \
	intercept.h \
	range.h \
	slice.h \

slice: $(slice_la_SOURCES) $(slice_la_HEADERS)
	tsxs -v -o slice.so $(slice_la_SOURCES)
#	tsxs -v -i -o slice.so $(slice_la_SOURCES)

install: slice
	tsxs -v -i slice.so

CXX = c++ -std=c++11
#CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
TSINCLUDE = $(shell tsxs -q INCLUDEDIR)
#PREFIX = $(shell tsxs -q PREFIX)
#LIBS = -L$(PREFIX)/lib -latscppapi
#LIBS = $(PREFIX)/lib/libtsutil.la

slice_test: slice_test.cc ContentRange.cc range.cc
	$(CXX) -o $@ $^ $(CXXFLAGS) -I$(TSINCLUDE) -DUNITTEST

clean: 
	rm -fv *.lo *.so slice_test
