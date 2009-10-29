# Makefile.am for Http
#

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http2/remap \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/iocore/cache \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/net \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/cluster \
  -I$(top_srcdir)/iocore/hostdb \
  -I$(top_srcdir)/iocore/dns

noinst_HEADERS = HttpProxyServerMain.h
noinst_LIBRARIES = libhttp.a

libhttp_a_SOURCES = \
  Hash_Table.cc \
  Hash_Table.h \
  HttpAccept.cc \
  HttpAccept.h \
  HttpAssert.h \
  HttpBodyFactory.cc \
  HttpBodyFactory.h \
  HttpCacheSM.cc \
  HttpCacheSM.h \
  HttpClientSession.cc \
  HttpClientSession.h \
  HttpConfig.cc \
  HttpConfig.h \
  HttpConnectionCount.cc \
  HttpConnectionCount.h \
  HttpDebugNames.cc \
  HttpDebugNames.h \
  HttpMessageBody.cc \
  HttpMessageBody.h \
  HttpPages.cc \
  HttpPages.h \
  HttpProxyServerMain.cc \
  HttpServerSession.cc \
  HttpServerSession.h \
  HttpSessionManager.cc \
  HttpSessionManager.h \
  HttpSM.cc \
  HttpSM.h \
  HttpTransactCache.cc \
  HttpTransactCache.h \
  HttpTransact.cc \
  HttpTransact.h \
  HttpTransactHeaders.cc \
  HttpTransactHeaders.h \
  HttpTunnel.cc \
  HttpTunnel.h \
  HttpUpdateSM.cc \
  HttpUpdateSM.h \
  HttpUpdateTester.cc \
  remap/AclFiltering.cc \
  remap/AclFiltering.h \
  remap/RemapPluginInfo.cc \
  remap/RemapPluginInfo.h \
  remap/RemapPlugins.cc \
  remap/RemapPlugins.h \
  remap/RemapProcessor.cc \
  remap/RemapProcessor.h \
  remap/StringHash.cc \
  remap/StringHash.h \
  remap/UmsHelper.cc \
  remap/UmsHelper.h \
  remap/UrlMapping.cc \
  remap/UrlMapping.h \
  remap/UrlRewrite.cc \
  remap/UrlRewrite.h
