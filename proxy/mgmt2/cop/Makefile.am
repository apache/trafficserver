#
# Makefile.am for the Enterprise Management module.
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/cli \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy \
  -DPREFIX=\"$(prefix)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\"

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

bin_PROGRAMS = traffic_cop

traffic_cop_SOURCES = TrafficCop.cc
traffic_cop_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_cop_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTCL@ \
  @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTHREAD@ @LIBDL@ @LIBRT@ @LIBICONV@
