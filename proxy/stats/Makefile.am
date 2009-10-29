# Makefile.am for Stats
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/logging

noinst_LIBRARIES = libstats.a

libstats_a_SOURCES = \
  CoupledStats.cc \
  CoupledStats.h \
  Stats.cc \
  Stats.h
