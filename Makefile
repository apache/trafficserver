all: slice

slice_la_SOURCES = \
	ContentRange.cc \
	HttpHeader.cc \
	Range.cc \
	intercept.cc \
	slice.cc \

slice_la_HEADERS = \
	ContentRange.h \
	HttpHeader.h \
	Range.h \
	intercept.h \
	slice.h \

slice: $(slice_la_SOURCES) $(slice_la_HEADERS)
	tsxs -v -i -o slice.so $(slice_la_SOURCES)
#	tsxs -v -o slice.so $(slice_la_SOURCES)

install: slice
	tsxs -v -i slice.so

crr_slice: crr_slice.cc
	tsxs -v -i -o crr_slice.so $^

install: slice
	tsxs -v -i slice.so
	tsxs -v -i crr_slice.so

CXX = c++ -std=c++11
#CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
TSINCLUDE = $(shell tsxs -q INCLUDEDIR)
#PREFIX = $(shell tsxs -q PREFIX)
#LIBS = -L$(PREFIX)/lib -latscppapi
#LIBS = $(PREFIX)/lib/libtsutil.la

slice_test: slice_test.cc ContentRange.cc Range.cc
	$(CXX) -o $@ $^ $(CXXFLAGS) -I$(TSINCLUDE) -DUNITTEST

clean: 
	rm -fv *.lo *.so slice_test
