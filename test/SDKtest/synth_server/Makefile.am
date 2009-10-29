# 
# Makefile.am for SDKtest server
#

DEFS = @API_DEFS@ 

INCLUDES = -I$(srcdir)/api

noinst_PROGRAMS = SDKtest_server	

SDKtest_server_SOURCES = synth_server.c

SDKtest_server_LDFLAGS = @LIBM@ @LIBSOCKET@ @LIBNSL@ @LIBDL@



clean:
	-$(RM) -rf Templates.DB cxx_repository .deps $(noinst_PROGRAMS)
