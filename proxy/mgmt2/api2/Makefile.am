#
# Makefile.am for the Enterprise Management module.
#

SUBDIRS = remote

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/cli \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/iocore \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/api2

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libmgmtapilocal.a

libmgmtapilocal_a_SOURCES = \
  include/INKMgmtAPI.h \
  CfgContextDefs.h \
  CfgContextImpl.cc \
  CfgContextImpl.h \
  CfgContextManager.cc \
  CfgContextManager.h \
  CfgContextUtils.cc \
  CfgContextUtils.h \
  CoreAPI.cc \
  CoreAPI.h \
  CoreAPIShared.cc \
  CoreAPIShared.h \
  EventCallback.cc \
  EventCallback.h \
  EventControlMain.cc \
  EventControlMain.h \
  GenericParser.cc \
  GenericParser.h \
  INKMgmtAPI.cc \
  MgmtPlugin.cc \
  MgmtPlugin.h \
  NetworkUtilsDefs.h \
  NetworkUtilsLocal.cc \
  NetworkUtilsLocal.h \
  TSControlMain.cc \
  TSControlMain.h
