# Makefile.am for Hdrs
#

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/libinktomi++

noinst_LIBRARIES = libhdrs.a
EXTRA_PROGRAMS = load_http_hdr 

# Http library source files.
libhdrs_a_SOURCES = \
  HdrHeap.cc \
  HdrHeap.h \
  HdrTest.cc \
  HdrTest.h \
  HdrToken.cc \
  HdrToken.h \
  HdrTSOnly.cc \
  HdrUtils.cc \
  HdrUtils.h \
  HTTP.cc \
  HTTP.h \
  HttpCompat.cc \
  HttpCompat.h \
  MIME.cc \
  MIME.h \
  SDKAllocator.cc \
  SDKAllocator.h \
  URL.cc \
  URL.h

load_http_hdr_SOURCES = \
  HdrHeap.h \
  HTTP.h \
  load_http_hdr.cc \
  MIME.h

load_http_hdr_LDADD = -L. -lhdrs \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTCL@ @LIBRT@ @LIBTHREAD@
load_http_hdr_LDFLAGS = @EXTRA_CXX_LDFLAGS@
