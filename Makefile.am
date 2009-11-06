#
# Top-level Makefile.am for Traffic Server.
#
# 
#

# Local Macros
# http://www.gnu.org/software/automake/manual/automake.html#Local-Macros
ACLOCAL_AMFLAGS = -I m4

if STANDALONE_IOCORE
SUBDIRS =  libinktomi++ iocore librecords proxy doc
else
SUBDIRS =  libinktomi++ iocore librecords proxy doc install
endif

doxygen:
	$(MAKE) -C doc doxygen

help:
	@echo 'all            default target for building the package' && \
   echo 'check          run the test suite, if any' && \
   echo 'clean          remove whatever make created' && \
   echo 'distclean      remove whatever configure created' && \
   echo 'dist           recreate source package' && \
   echo 'distcheck      verify dist by performing VPATH build and then distclean' && \
   echo 'doxygen        generate doxygen docs in doc/html dir' && \
   echo 'help           display this list of make targets' && \
   echo 'install        install by copying the built files to system-wide dirs' && \
   echo 'install-strip  same as install but then strips debugging symbols' && \
   echo 'uninstall      opposite of install, removes installed files'

