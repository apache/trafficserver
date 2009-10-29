AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/proxy/hdrs

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libpreparse.a

libpreparse_a_SOURCES = \
  IPRange.cc \
  IPRange.h \
  RemapReadConfig.cc \
  SocksParser.cc \
  StoreReadConfig.cc
