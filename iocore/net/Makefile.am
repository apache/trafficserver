# Makefile.am for the traffic/iocore/net hierarchy
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/cache \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/iocore/cluster \
  -I$(top_srcdir)/iocore/hostdb \
  -I$(top_srcdir)/iocore/dns \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2

DEFS += @IOCORE_MODULARIZED_DEFS@

noinst_LIBRARIES = libinknet.a

libinknet_a_SOURCES = \
  Connection.cc \
  DetailedLog.cc \
  DetailedLog.h \
  I_NetConfig.h \
  I_NetErrno.h \
  I_Net.h \
  I_NetProcessor.h \
  I_NetVConnection.h \
  Inline.cc \
  I_Socks.h \
  I_UDPConnection.h \
  I_UDPNet.h \
  I_UDPPacket.h \
  Net.cc \
  NetConfig.cc \
  NetVConnection.cc \
  NetVCTest.cc \
  P_CompletionUtil.h \
  P_Connection.h \
  P_InkBulkIO.h \
  P_LibBulkIO.h \
  P_NetAccept.h \
  P_Net.h \
  P_NetVConnection.h \
  P_NetVCTest.h \
  P_Socks.h \
  P_SSLCertLookup.h \
  P_SSLConfig.h \
  P_SSLNetAccept.h \
  P_SSLNetProcessor.h \
  P_SSLNetVConnection.h \
  P_UDPConnection.h \
  P_UDPIOEvent.h \
  P_UDPNet.h \
  P_UDPPacket.h \
  P_UnixCompletionUtil.h \
  P_UnixNet.h \
  P_UnixNetProcessor.h \
  P_UnixNetState.h \
  P_UnixNetVConnection.h \
  P_UnixPollDescriptor.h \
  P_UnixUDPConnection.h \
  Socks.cc \
  SSLCertLookup.cc \
  SSLConfig.cc \
  SSLNet.cc \
  SSLNetVConnection.cc \
  SSLUnixNet.cc \
  UDPIOEvent.cc \
  UnixConnection.cc \
  UnixNetAccept.cc \
  UnixNet.cc \
  UnixNetPages.cc \
  UnixNetProcessor.cc \
  UnixNetVConnection.cc \
  UnixUDPConnection.cc \
  UnixUDPNet.cc

