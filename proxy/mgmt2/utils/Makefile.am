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
  -I$(top_srcdir)/proxy

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

# header files used by other libraries
EXTRA_DIST = MgmtHashTable.h MgmtSocket.h

noinst_LIBRARIES = libutils_lm.a libutils_p.a

libutils_lm_a_CPPFLAGS = -DLOCAL_MANAGER $(AM_CPPFLAGS)
libutils_lm_a_SOURCES = \
  $(top_srcdir)/libinktomi++/MatcherUtils.cc \
  $(top_srcdir)/proxy/DiagsConfig.cc \
  EnvBlock.cc \
  EnvBlock.h \
  ExpandingArray.cc \
  ExpandingArray.h \
  IpLookup.cc \
  IpLookup.h \
  MgmtConverter.cc \
  MgmtConverter.h \
  MgmtPing.cc \
  MgmtPing.h \
  MgmtSchema.cc \
  MgmtSchema.h \
  MgmtServerRPC.cc \
  MgmtServerRPC.h \
  MgmtUtils.cc \
  MgmtUtils.h \
  SimpleQueue.cc \
  SimpleQueue.h \
  WebMgmtUtils.cc \
  WebMgmtUtils.h \
  XmlUtils.cc \
  XmlUtils.h

libutils_p_a_SOURCES = \
  IpLookup.cc \
  IpLookup.h \
  MgmtUtils.cc \
  MgmtUtils.h \
  XmlUtils.cc \
  XmlUtils.h
