# Makefile.am for Congestion Control
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/librecords \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/nntp \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/libinktomi++

noinst_LIBRARIES = libCongestionControl.a

libCongestionControl_a_SOURCES = \
  Congestion.cc \
  Congestion.h \
  CongestionDB.cc \
  CongestionDB.h \
  CongestionStats.cc \
  CongestionStats.h \
  CongestionTest.cc \
  MT_hashtable.h
