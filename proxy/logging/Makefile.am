# Makefile.am for Logging
#

DEFS += -DIOCORE_LOG_COLLATION

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/http2/remap \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/hdrs

EXTRA_DIST = LogStandalone.cc

noinst_LIBRARIES = liblogging.a

liblogging_a_SOURCES = \
  LogAccess.cc \
  LogAccess.h \
  LogAccessHttp.cc \
  LogAccessHttp.h \
  LogAccessICP.cc \
  LogAccessICP.h \
  LogBuffer.cc \
  LogBuffer.h \
  LogBufferSink.h \
  LogBufferV1.h \
  Log.cc \
  Log.h \
  LogCollationAccept.cc \
  LogCollationAccept.h \
  LogCollationBase.h \
  LogCollationClientSM.cc \
  LogCollationClientSM.h \
  LogCollationHostSM.cc \
  LogCollationHostSM.h \
  LogConfig.cc \
  LogConfig.h \
  LogFieldAliasMap.cc \
  LogFieldAliasMap.h \
  LogField.cc \
  LogField.h \
  LogFile.cc \
  LogFile.h \
  LogFilter.cc \
  LogFilter.h \
  LogFormat.cc \
  LogFormat.h \
  LogFormatType.h \
  LogHost.cc \
  LogHost.h \
  LogLimits.h \
  LogObject.cc \
  LogObject.h \
  LogSock.cc \
  LogSock.h \
  LogUtils.cc \
  LogUtils.h
