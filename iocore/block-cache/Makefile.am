# Makefile.am for the traffic/iocore/block-cache hierarchy
#


INCLUDES += -I$(top_srcdir)/iocore/cluster
INCLUDES += -I$(top_srcdir)/librecords
INCLUDES += -I$(top_srcdir)/proxy/http2
INCLUDES += -I$(top_srcdir)/proxy/hdrs
INCLUDES += -I$(top_srcdir)/proxy

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkblockcache.a

libinkblockcache_a_SOURCES = \
		BlockCacheSegmentVConnection.cc

