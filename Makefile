all: slicer 

slicer_la_SOURCES = \
	slicer.cc \
	config.cc \
	config.h \
	util.cc \
	util.h \

slicer: $(slicer_la_SOURCES)
	tsxs -v -i -o slicer.so slicer.cc config.cc util.cc

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
