# Makefile.am for NTLM Client
#

AM_CPPFLAGS = \
  -I$(top_srcdir)/librecords \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/http2 \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/libinktomi++

noinst_LIBRARIES = libdnscache.a

libdnscache_a_SOURCES =	DNS_cache.cc DNS_cache.h
