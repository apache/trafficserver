# Makefile.am for libfastIO.a
#
# 

FASTIO_MACH_DEFS=-fPIC -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 -D_POSIX_PTHREAD_SEMANTICS=1
FASTIO_LIB_DEFS=-DDEBUG

DEFS += $(FASTIO_MACH_DEFS) $(FASTIO_LIB_DEFS)
AM_CPPFLAGS = -I$(top_srcdir)/proxy/iocore/fastio/include

# Pure tools won't work with '-r' so don't use them.
CXXLINK = $(LD) $(LDFLAGS) -o $@

noinst_LIBRARIES =libfastIO.a

DEFS += $(FASTIO_MACH_DEFS) $(FASTIO_LIB_DEFS)

noinst_PROGRAMS =

# libfastIO source files
libfastIO_a_SOURCES =  libfastio.c 
