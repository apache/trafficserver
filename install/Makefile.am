# Makefile.am for install
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/cache \
  -I$(top_srcdir)/proxy/logging

dist_bin_SCRIPTS = \
  start_traffic_shell \
  trafficserver

bin_PROGRAMS = update_records

update_records_SOURCES = update_records.cc 
update_records_LDFLAGS = @EXTRA_CXX_LDFLAGS@
update_records_LDADD =	\
			$(top_builddir)/proxy/mgmt2/librecords.a \
			$(top_builddir)/libinktomi++/libinktomi++.a \
			@LIBTHREAD@ @LIBRT@ @LIBTCL@ @LIBDL@ @LIBSSL@

