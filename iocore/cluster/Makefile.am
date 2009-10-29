# Makefile.am for the traffic/iocore/cluster hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/net \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/cache \
  -I$(top_srcdir)/iocore/dns \
  -I$(top_srcdir)/iocore/hostdb \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/api/include \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinkcluster.a

libinkcluster_a_SOURCES = \
  ClusterAPI.cc \
  ClusterCache.cc \
  ClusterConfig.cc \
  ClusterHandler.cc \
  ClusterHandlerBase.cc \
  ClusterHash.cc \
  ClusterLib.cc \
  ClusterLoadMonitor.cc \
  ClusterMachine.cc \
  ClusterProcessor.cc \
  ClusterRPC.cc \
  ClusterVConnection.cc \
  P_Cluster.h \
  P_ClusterCache.h \
  P_ClusterCacheInternal.h \
  P_ClusterHandler.h \
  P_ClusterInline.h \
  P_ClusterInternal.h \
  P_ClusterLib.h \
  P_ClusterLoadMonitor.h \
  P_ClusterMachine.h \
  P_TimeTrace.h

