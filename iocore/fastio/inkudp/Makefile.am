# Makefile.am for inkudp
#

INKFASTUDP_KFLAGS=-D_KERNEL
INKFASTUDP_MACH_INCLUDES=
INKFASTUDP_MACH_DEFS=-DSunOS56 -Dsparc

AM_CPPFLAGS = $(INKFASTUDP_MACH_INCLUDES) \
  -I$(top_srcdir)/proxy/iocore/fastio/include

DEFS += $(INKFASTUDP_DEFS) $(INKFASTUDP_MACH_DEFS)
CFLAGS = $(INKFASTUDP_KFLAGS) $(DEBUG)

# Pure tools won't work with '-r' so don't use them.
LINK = $(LD) $(LDFLAGS) -o $@

EXTRA_PROGRAMS = inkudp

# Solaris
inkudp_SOURCES =solaris.c \
		inkudp_ioctl_util.c\
		inkudp_ioctl_fini.c\
		inkudp_sched.c
inkudp_LDFLAGS = -r -dy -N "pseudo/inkfio"
inkudp_LDADD = 

