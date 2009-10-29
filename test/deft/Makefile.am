
INCLUDES = -I$(top_srcdir)/libinktomi++

noinst_PROGRAMS = proc_manager test_exec test_log_collate

proc_manager_SOURCES = proc_manager.cc \
		       log_sender.cc \
		       raf_cmd.cc \
		       sio_buffer.cc \
		       sio_loop.cc \
		       sio_raf_server.cc \
		       test_utils.cc

proc_manager_LDFLAGS = @EXTRA_CXX_LDFLAGS@
proc_manager_LDADD = $(top_builddir)/libinktomi++/libinktomi++.a \
		     @LIBSOCKET@ @LIBNSL@ \
		     @LIBRESOLV@ @LIBRT@ @LIBTHREAD@ -lm @LIBDL@


test_log_collate_SOURCES = test_log_collate.cc \
	  	           log_sender.cc \
			   raf_cmd.cc \
			   sio_buffer.cc \
			   sio_loop.cc \
			   sio_raf_server.cc \
		           test_utils.cc

test_log_collate_LDFLAGS = @EXTRA_CXX_LDFLAGS@
test_log_collate_LDADD = $(top_builddir)/libinktomi++/libinktomi++.a \
		     	 @LIBSOCKET@ @LIBNSL@ \
		     	 @LIBRESOLV@ @LIBRT@ @LIBTHREAD@ -lm @LIBDL@

test_exec_SOURCES = test_exec.cc \
		    raf_cmd.cc \
		    remote_start.cc \
		    sio_buffer.cc \
		    sio_loop.cc \
	            test_interp_glue_wrap.c \
		    test_exec_perl.c \
		    test_group.cc \
		    test_results.cc \
	            test_utils.cc

test_exec_LDFLAGS = @EXTRA_CXX_LDFLAGS@
test_exec_LDADD = $(top_builddir)/libinktomi++/libinktomi++.a \
		     -L/usr/local/lib  \
		     $(top_builddir)/f_pkg/misc/perl5.6.1/lib/DynaLoader.a \
		     -L$(top_builddir)/f_pkg/misc/perl5.6.1/lib -lperl \
		     @LIBSOCKET@ @LIBNSL@ \
		     @LIBRESOLV@ @LIBRT@ @LIBTCL@ @LIBDL@ @LIBTHREAD@ \
		     @LIBCRYPT@ -lm

test_interp_glue_wrap.c: test_interp_glue.i test_interp_glue.h
	swig -perl5 -static -I$(top_srcdir)/test test_interp_glue.i

pkg_ts:
	$(mkinstalldirs) $(top_srcdir)/deft-install
	$(mkinstalldirs) $(top_srcdir)/deft-install/packages
	$(srcdir)/deft_package_ts.pl -b $(top_builddir) -o $(top_srcdir)/deft-install/packages -R

install-exec-local: proc_manager test_exec test_log_collate
	$(mkinstalldirs) $(top_srcdir)/deft-install
	$(mkinstalldirs) $(top_srcdir)/deft-install/packages
	-$(RM) $(top_srcdir)/deft-install/scripts
	$(LN_S) ../test/scripts $(top_srcdir)/deft-install/scripts
	-$(RM) $(top_srcdir)/deft-install/parsers
	$(LN_S) ../test/parsers $(top_srcdir)/deft-install/parsers
	-$(RM) $(top_srcdir)/deft-install/defs
	$(LN_S) ../test/defs $(top_srcdir)/deft-install/defs
	cp proc_manager $(top_srcdir)/deft-install/packages/proc_manager-$(TFW_PACKAGE_SUFFIX)
	cp test_exec $(top_srcdir)/deft-install/packages/test_exec-$(TFW_PACKAGE_SUFFIX)
	cp test_log_collate $(top_srcdir)/deft-install/packages/test_log_collate-$(TFW_PACKAGE_SUFFIX)
	cp TestExec.pm $(top_srcdir)/deft-install/
	cp $(top_srcdir)/test/deft_profile.sh $(top_srcdir)/deft-install/
	cp $(top_srcdir)/test/run_test.sh $(top_srcdir)/deft-install/
	cp $(top_srcdir)/test/run_test.pl $(top_srcdir)/deft-install/
	perl $(top_srcdir)/test/setup_pkg_links.pl /home/inktomi/TrafficServer/TestToolPackages $(top_srcdir)/deft-install/packages

clean-local:
	-$(RM) -rf test_interp_glue_wrap.c TestExec.pm

