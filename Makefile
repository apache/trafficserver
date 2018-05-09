all: slicer 

slicer_la_SOURCES = \
	slicer.cc \
	config.cc \
	data.cc \
	util.cc \

slicer_la_HEADERS = \
	config.h \
	data.h \
	util.h \

slicer: $(slicer_la_SOURCES) $(slicer_la_HEADERS)
	tsxs -v -i -o slicer.so $(slicer_la_SOURCES)

CXX = c++ -std=c++11
#CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
CXXFLAGS = -pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -O3 -fno-strict-aliasing -Wno-invalid-offsetof  -mcx16
INCLUDE = $(shell tsxs -q INCLUDEDIR)
PREFIX = $(shell tsxs -q PREFIX)
#LIBS = -L$(PREFIX)/lib -latscppapi
#LIBS = $(PREFIX)/lib/libtsutil.la

slicer_test: slicer_test.cc util.cc
	$(CXX) -o $@ $^ $(CXXFLAGS) -I$(INCLUDE)

clean: 
	rm -fv *.lo *.so 
