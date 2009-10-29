#
# Makefile.am for the Enterprise Management module.
#

SUFFIXES = .java .class .jar .my .def .stb .html

AM_CPPFLAGS = \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2/tools

dist_bin_SCRIPTS = \
  cli_setdomain.tcl \
  cli_sethostname.tcl \
  cli_setrouter.tcl \
  script_configs.tcl

bin_PROGRAMS = traffic_shell
traffic_shell_SOURCES = \
  cliAppInit.cc \
  CliCreateCommands.h \
  cliMain.cc \
  CliMgmtUtils.h \
  ConfigCmd.h \
  createArgument.h \
  createCommand.h \
  definitions.h \
  hashtable.h \
  ShowCmd.h

noinst_LIBRARIES = libtrafficshell.a
libtrafficshell_a_SOURCES = \
  CliCreateCommands.cc \
  CliCreateCommands.h \
  CliCreateUpgradeCmd.cc \
  CliDisplay.cc \
  CliDisplay.h \
  CliMgmtUtils.cc \
  CliMgmtUtils.h \
  cliParseArg.cc \
  cli_scriptpaths.h \
  commandOptions.h \
  ConfigCmd.cc \
  ConfigCmd.h \
  ConfigUpgradeCmd.h \
  ConfigUpgradeReadCmd.cc \
  ConfigUpgradeWriteCmd.cc \
  createArgument.cc \
  createArgument.h \
  createCommand.cc \
  createCommand.h \
  definitions.h \
  hashtable.cc \
  hashtable.h \
  processArgument.cc \
  ShowCmd.cc \
  ShowCmd.h \
  UtilCmds.cc \
  UtilCmds.h

traffic_shell_LDFLAGS =	$(EXTRA_CXX_LDFLAGS)

traffic_shell_LDADD = \
  $(top_builddir)/proxy/mgmt2/api2/remote/libmgmtapiremote.a \
  -ltcl \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  libtrafficshell.a \
  $(top_builddir)/proxy/mgmt2/tools/libinksysapi.a \
  $(top_builddir)/proxy/mgmt2/tools/libinkconfigapi.a \
  $(top_builddir)/proxy/mgmt2/utils/libutils_lm.a \
  -ldl \
  -lm \
  @LIBEXPAT@ @LIBSSL@ \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBRT@

libtrafficshell_a_LIBADD = \
  $(libtrafficshell_a_OBJECTS) \
  $(libtrafficshell_a_DEPENDENCIES)
