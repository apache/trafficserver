# Makefile.am for the traffic/iocore/net hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/net \
  -I$(top_srcdir)/iocore/cache \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/iocore/cluster \
  -I$(top_srcdir)/iocore/hostdb \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/nntp \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/snmp \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/mgmt2/web2/include \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkdns.a

libinkdns_a_SOURCES = \
  DNS.cc \
  DNSConnection.cc \
  I_DNS.h \
  I_DNSProcessor.h \
  I_SplitDNS.h \
  I_SplitDNSProcessor.h \
  Inline.cc \
  P_DNS.h \
  P_DNSConnection.h \
  P_DNSProcessor.h \
  P_SplitDNS.h \
  P_SplitDNSProcessor.h \
  SplitDNS.cc \
  SRV.cc \
  SRV.h

