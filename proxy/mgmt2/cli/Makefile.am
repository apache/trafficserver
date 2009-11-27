#
# Makefile.am for the Enterprise Management module.
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libcli_lm.a libcli_tl.a

bin_PROGRAMS = traffic_line

libcli_tl_a_SOURCES = \
  clientCLI.cc \
  clientCLI.h \
  CliUtils.cc \
  CliUtils.h

libcli_lm_a_SOURCES = \
  AbsEventHandler.h \
  CLI.cc \
  CLI.h \
  CLIconfigure.cc \
  CLIconfigure.h \
  CLIeventHandler.cc \
  CLIeventHandler.h \
  CLIhelp.cc \
  CLIlineBuffer.cc \
  CLIlineBuffer.h \
  CLImonitor.cc \
  CLImonitor.h \
  CliUtils.cc \
  CliUtils.h \
  FSM.cc \
  FSM.h

traffic_line_SOURCES = \
  clientCLI.h \
  CliMain.cc

traffic_line_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_line_LDADD = \
  libcli_tl.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  $(top_builddir)/librecords/libreclocal.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTCL@ @LIBRT@ @LIBDL@ @LIBICONV@
