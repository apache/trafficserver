# Makefile.am for the traffic/iocore/utils hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/libinktomi++

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkutils.a

libinkutils_a_SOURCES = \
  I_Machine.h \
  I_MTInteractor.h \
  I_OneWayMultiTunnel.h \
  I_OneWayTunnel.h \
  I_OpQueue.h \
  I_RadixSort.h \
  MTInteractor.cc \
  Machine.cc \
  OneWayMultiTunnel.cc \
  OneWayTunnel.cc \
  OpQueue.cc \
  RadixSort.cc

