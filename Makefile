all: slice 

slice_la_SOURCES = \
	HttpHeader.cc \
	intercept.cc \
	slice.cc \

slice_la_HEADERS = \
	HttpHeader.h \
	intercept.h \
	slice.h \

slice: $(slice_la_SOURCES) $(slice_la_HEADERS)
	tsxs -v -i -o slice.so $(slice_la_SOURCES)

CXX = c++ -std=c++11
#CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
INCLUDE = $(shell tsxs -q INCLUDEDIR)
PREFIX = $(shell tsxs -q PREFIX)
#LIBS = -L$(PREFIX)/lib -latscppapi
#LIBS = $(PREFIX)/lib/libtsutil.la

slice_test: slice_test.cc util.cc
	$(CXX) -o $@ $^ $(CXXFLAGS) -I$(INCLUDE)

clean: 
	rm -fv *.lo *.so 
