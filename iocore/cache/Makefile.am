# Makefile.am for the traffic/iocore/cache hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/net \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/cluster \
  -I$(top_srcdir)/iocore/hostdb \
  -I$(top_srcdir)/iocore/dns \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/http2/remap \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -DSYSCONFDIR=\"$(sysconfdir)\"

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkcache.a

libinkcache_a_SOURCES = \
  Cache.cc \
  CacheDir.cc \
  CacheDisk.cc \
  CacheHosting.cc \
  CacheHttp.cc \
  CacheLink.cc \
  CachePages.cc \
  CachePagesInternal.cc \
  CachePart.cc \
  CacheRead.cc \
  CacheWrite.cc \
  I_Cache.h \
  I_CacheDefs.h \
  I_Store.h \
  Inline.cc \
  NewCacheVC.cc \
  NewCacheVC.h \
  P_Cache.h \
  P_CacheArray.h \
  P_CacheDir.h \
  P_CacheDisk.h \
  P_CacheHosting.h \
  P_CacheHttp.h \
  P_CacheInternal.h \
  P_CachePart.h \
  P_CacheTest.h \
  P_RamCache.h \
  RamCache.cc \
  Store.cc

