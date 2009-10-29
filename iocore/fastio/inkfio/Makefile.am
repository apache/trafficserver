# Makefile.am for inkfio driver
#

INKFASTIO_KFLAGS=-D_KERNEL
INKFASTIO_MACH_INCLUDES=-I/net/devi/usr/include
INKFASTIO_MACH_DEFS=-DSunOS56 -Dsparc

AM_CPPFLAGS = $(INKFASTIO_MACH_INCLUDES) \
  -I$(top_srcdir)/proxy/iocore/fastio/include

DEFS += $(INKFASTIO_DEFS) $(INKFASTIO_MACH_DEFS)
CFLAGS = $(INKFASTIO_KFLAGS) $(DEBUG)

# Pure tools won't work with '-r' so don't use them.
LINK = $(LD) $(LDFLAGS) -o $@

EXTRA_PROGRAMS = inkfio

# Solaris
inkfio_SOURCES = fio_dev_sol.c \
		fio_ioctl.c \
		fio_request.c \
		fio_vsession.c

inkfio_LDFLAGS = -r 
inkfio_LDADD =

