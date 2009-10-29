# 
# Makefile.am for SDKtest client
#

INCLUDES = -I$(srcdir)/api

noinst_PROGRAMS = SDKtest_client

SDKtest_client_SOURCES = DoTest.cc \
			 LoadGenerator.cc \
			 Config.cc \
			 Main.cc \
			 Hist.cc \
			 Plugin.cc

SDKtest_client_LDFLAGS = @API_DEFS@
SDKtest_client_LDADD = @LIBM@ @LIBSOCKET@ @LIBNSL@ @LIBDL@ @LIBSSL@

clean:
	-$(RM) -rf Templates.DB cxx_repository .deps $(noinst_PROGRAMS)
