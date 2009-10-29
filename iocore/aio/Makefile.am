# Makefile.am for the traffic/iocore/aio hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/libinktomi++

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkaio.a

libinkaio_a_SOURCES = AIO.cc I_AIO.h P_AIO.h inkaio.h

