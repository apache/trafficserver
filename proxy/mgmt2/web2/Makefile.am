#
# Makefile.am for the Enterprise Management module.
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/cli \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/mgmt2/tools \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs

MGMT_DEFS = @MGMT_DEFS@
DEFS  += $(MGMT_DEFS)

noinst_LIBRARIES = libweb.a

libweb_a_SOURCES = \
  MgmtAllow.cc \
  MgmtAllow.h \
  MgmtRaf.h \
  WebCompatibility.cc \
  WebCompatibility.h \
  WebConfig.cc \
  WebConfig.h \
  WebConfigRender.cc \
  WebConfigRender.h \
  WebGlobals.h \
  WebHttpAuth.cc \
  WebHttpAuth.h \
  WebHttp.cc \
  WebHttp.h \
  WebHttpContext.cc \
  WebHttpContext.h \
  WebHttpLog.cc \
  WebHttpLog.h \
  WebHttpMessage.cc \
  WebHttpMessage.h \
  WebHttpRender.cc \
  WebHttpRender.h \
  WebHttpSession.cc \
  WebHttpSession.h \
  WebHttpTree.cc \
  WebHttpTree.h \
  WebIntrMain.cc \
  WebIntrMain.h \
  WebOverview.cc \
  WebOverview.h \
  WebReconfig.cc \
  WebReconfig.h \
  WebUtils.cc \
  WebUtils.h
