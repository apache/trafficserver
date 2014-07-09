SHELL := /bin/bash
TSXS?=tsxs
# Specify BUILDTYPE=Debug for a debug build
BUILDTYPE?=Release
MOD_PAGESPEED_DIR=$(shell pwd)/psol/include/
PAGESPEED_OUT=$(shell pwd)/psol/lib/$(BUILDTYPE)/linux/x64/


os_name=unknown_os
arch_name=ia32
uname_os=$(shell uname)
uname_arch=$(shell uname -m)

ifeq ($(uname_os),Linux)
  os_name=linux
endif

ifeq ($(uname_arch), x86_64)
  arch_name=x64
endif
ifeq ($(uname_arch), amd64)
  arch_name=x64
endif

INC =-I$(MOD_PAGESPEED_DIR)\
 -I$(MOD_PAGESPEED_DIR)third_party/chromium/src/\
 -I$(MOD_PAGESPEED_DIR)third_party/google-sparsehash/src\
 -I$(MOD_PAGESPEED_DIR)third_party/google-sparsehash/gen/arch/$(os_name)/$(arch_name)/include\
 -I$(MOD_PAGESPEED_DIR)third_party/protobuf/src\
 -I$(MOD_PAGESPEED_DIR)third_party/re2/src\
 -I$(MOD_PAGESPEED_DIR)third_party/out/$(BUILDTYPE)/obj/gen\
 -I$(MOD_PAGESPEED_DIR)third_party/apr/src/include/\
 -I$(MOD_PAGESPEED_DIR)third_party/aprutil/src/include/\
 -I$(MOD_PAGESPEED_DIR)third_party/apr/gen/arch/$(os_name)/$(arch_name)/include/\
 -I$(MOD_PAGESPEED_DIR)third_party/aprutil/gen/arch/$(os_name)/$(arch_name)/include/\
 -I$(MOD_PAGESPEED_DIR)out/$(BUILDTYPE)/obj/gen\
 -I$(MOD_PAGESPEED_DIR)out/$(BUILDTYPE)/obj/gen/protoc_out/instaweb

PSOL_LIBS = $(PAGESPEED_OUT)pagespeed_automatic.a $(PAGESPEED_OUT)libserf.a $(PAGESPEED_OUT)libaprutil.a $(PAGESPEED_OUT)libapr.a

%.so: psol %.cc
# https://github.com/pagespeed/ngx_pagespeed/issues/433: it would be nice to have -Wall -Werror, only suppressing when needed.
	g++ $(INC) -shared -o ats_speed.so -g -pipe -O3 -fpic $(MOD_PAGESPEED_DIR)/out/$(BUILDTYPE)/obj/gen/data2c_out/instaweb/net/instaweb/apache/install/mod_pagespeed_example/*.cc $(MOD_PAGESPEED_DIR)/net/instaweb/system/*.cc  *.cc -lstdc++ -lstdc++  -lpthread $(PSOL_LIBS) -lrt

all: psol gzip/gzip.so ats_speed.so

1.7.30.4.tar.gz:
	wget --no-check-certificate https://dl.google.com/dl/page-speed/psol/1.7.30.4.tar.gz

psol/: 1.7.30.4.tar.gz
	tar -xzvf 1.7.30.4.tar.gz

gzip/gzip.so:
	cd gzip && make

install: all
	$(TSXS) -i -o ats_speed.so
	cd gzip && make install

cleanpsol:
	rm -rf psol/
	rm *.gz

clean:
	rm -f *.lo *.so *.o
	rm -f gzip/*.lo gzip/*.so gzip/*.o
