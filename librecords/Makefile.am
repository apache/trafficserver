# Makefile.am for the traffic/librecords
#

DEFS += @ink_with_modules_def@

AM_CPPFLAGS = \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/iocore/net \
  -I$(top_srcdir)/iocore/cache \
  -I$(top_srcdir)/iocore/aio \
  -I$(top_srcdir)/iocore/utils \
  -I$(top_srcdir)/iocore/cluster \
  -I$(top_srcdir)/iocore/dns \
  -I$(top_srcdir)/iocore/hostdb \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/snmp \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/libinktomi++ \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\"

noinst_LIBRARIES = libreclocal.a librecprocess.a

libreclocal_a_CXXFLAGS = $(ink_with_modules_local)
libreclocal_a_SOURCES = \
  I_RecAlarms.h \
  I_RecCore.h \
  I_RecDefs.h \
  I_RecEvents.h \
  I_RecLocal.h \
  I_RecMutex.h \
  I_RecSignals.h \
  P_RecCompatibility.h \
  P_RecCore.h \
  P_RecCore.i \
  P_RecDefs.h \
  P_RecLocal.h \
  P_RecMessage.h \
  P_RecTree.h \
  P_RecUtils.h \
  RecCompatibility.cc \
  RecCore.cc \
  RecLocal.cc \
  RecMessage.cc \
  RecMutex.cc \
  RecTree.cc \
  RecUtils.cc

librecprocess_a_CXXFLAGS = $(ink_with_modules_process)
librecprocess_a_SOURCES = \
  I_RecAlarms.h \
  I_RecCore.h \
  I_RecDefs.h \
  I_RecEvents.h \
  I_RecMutex.h \
  I_RecProcess.h \
  I_RecSignals.h \
  P_RecCompatibility.h \
  P_RecCore.h \
  P_RecCore.i \
  P_RecDefs.h \
  P_RecMessage.h \
  P_RecProcess.h \
  P_RecTree.h \
  P_RecUtils.h \
  RecCompatibility.cc \
  RecCore.cc \
  RecMessage.cc \
  RecMutex.cc \
  RecProcess.cc \
  RecTree.cc \
  RecUtils.cc

