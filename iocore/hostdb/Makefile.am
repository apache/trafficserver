# Makefile.am for the traffic/iocore/hostdb hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/net \
  -I$(top_srcdir)/iocore/cache \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/iocore/cluster \
  -I$(top_srcdir)/iocore/dns \
  -I$(top_srcdir)/iocore/hostdb \
  -I$(top_srcdir)/proxy/mgmt2/snmp \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy

DEFS += @IOCORE_MODULARIZED_DEFS@

EXTRA_DIST = I_HostDB.h

noinst_LIBRARIES = libinkhostdb.a

libinkhostdb_a_SOURCES = \
  HostDB.cc \
  I_HostDBProcessor.h \
  Inline.cc \
  MultiCache.cc \
  P_HostDB.h \
  P_HostDBProcessor.h \
  P_MultiCache.h

