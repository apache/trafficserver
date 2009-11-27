# Process this file with `autoreconf -i` to create a 'configure' file.

# Table of Contents
# 1. INITIALIZATION
# 2. SITE CONFIGURATION
# 3. CHECK FOR PROGRAMS
# 4. CHECK FOR LIBRARIES
# 5. CHECK FOR HEADERS
# 6. OUTPUT FILES

# -----------------------------------------------------------------------------
# 1. INITIALIZATION

AC_INIT([Traffic Server], [2.0.0], [trafficserver-dev@incubator.apache.org], [trafficserver])
AC_PREREQ([2.59])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror foreign no-installinfo no-installman 1.9.2])
AC_CONFIG_SRCDIR([proxy/Main.cc])

#
# Host detection
#

AC_CANONICAL_HOST

case $host_os in
  linux*)    AC_DEFINE_UNQUOTED([HOST_OS],[linux])   ;;
  darwin*)   AC_DEFINE_UNQUOTED([HOST_OS],[darwin])  ;;
  freebsd*)  AC_DEFINE_UNQUOTED([HOST_OS],[freebsd]) ;;
  *)         AC_DEFINE_UNQUOTED([HOST_OS],[unknown]) ;;
esac

#
# Build environment
#

build_person="`whoami`"
build_machine="`uname -n`"
AC_DEFINE_UNQUOTED(BUILD_PERSON, "$build_person")
AC_DEFINE_UNQUOTED(BUILD_MACHINE, "$build_machine")

# -----------------------------------------------------------------------------
# 2. SITE CONFIGURATION

#
# Debug
#

AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE([debug],
  [AS_HELP_STRING([--enable-debug],[turn on debugging])],
  [],
  [enable_debug=no]
)
AC_MSG_RESULT([$enable_debug])
AS_IF([test "x$enable_debug" = "xyes"],
  [AC_DEFINE([DEBUG])]
)

#
# Purify
#

AC_MSG_CHECKING([whether to enable purify])
AC_ARG_ENABLE([purify],
  [AS_HELP_STRING([--enable-purify],[enable support for Purify])],
  [],
  [enable_purify=no]
)
AC_MSG_RESULT([$enable_purify])
AS_IF([test "x$enable_purify" = "xyes"],
  [AC_DEFINE([PURIFY])]
)

#
# Disk InkAIO
#

AC_MSG_CHECKING([whether to enable disk InkAIO])
AC_ARG_ENABLE([diskinkaio],
  [AS_HELP_STRING([--enable-diskinkaio],[enable disk InkAIO])],
  [],
  [enable_diskinkaio=no]
)
AC_MSG_RESULT([$enable_diskinkaio])
AS_IF([test "x$enable_diskinkaio" = "xyes"],
  [AC_DEFINE([INKDISKAIO])]
)

#
# Micro
#

AC_MSG_CHECKING([whether to enable micro-TS])
AC_ARG_ENABLE([micro],
  [AS_HELP_STRING([--enable-micro],[enable micro-TS build])],
  [],
  [enable_micro=no]
)
AC_MSG_RESULT([$enable_micro])
AS_IF([test "x$enable_micro" = "xyes"],
  [AC_DEFINE(TS_MICRO)
   AC_DEFINE(INK_NO_ACL)
   AC_DEFINE(INK_NO_CLUSTER)
   AC_DEFINE(INK_NO_DIAGS)
   AC_DEFINE(INK_NO_FTP)
   AC_DEFINE(INK_NO_HOSTDB)
   AC_DEFINE(INK_NO_ICP)
   AC_DEFINE(INK_NO_LOG)
   AC_DEFINE(INK_NO_REMAP)
   AC_DEFINE(INK_NO_REVERSE)
   AC_DEFINE(INK_NO_SOCKS)
   AC_DEFINE(INK_NO_STAT_PAGES)
   AC_DEFINE(INK_NO_TESTS)
   AC_DEFINE(INK_USE_MUTEX_FOR_ATOMICLISTS)
   AC_DEFINE(INK_USE_MUTEX_FOR_FREELISTS)
   AC_DEFINE(USE_MD5_FOR_MMH)
   AC_DEFINE(_NO_FREELIST)
  ]
)

#
# Standalone iocore
#

AC_MSG_CHECKING([whether to build a standalone iocore])
AC_ARG_ENABLE([standalone-iocore],
  [AS_HELP_STRING([--enable-standalone-iocore],[build just standalone iocore])],
  [],
  [enable_standalone_iocore=no]
)
AC_MSG_RESULT([$enable_standalone_iocore])
AS_IF([test "x$enable_standalone_iocore" = "xyes"],
  [AC_SUBST([IOCORE_MODULARIZED_DEFS],[""])
   AC_SUBST([ink_with_modules_def],["-DREC_BUILD_STAND_ALONE"])
   AC_SUBST([ink_with_modules_local],[""])
   AC_SUBST([ink_with_modules_process],[""])
  ],
  [AC_SUBST([IOCORE_MODULARIZED_DEFS],["-DFIXME_NONMODULAR -DSPLIT_DNS -DNON_MODULAR -DHTTP_CACHE"])
   AC_SUBST([ink_with_modules_def],["-DREC_BUILD_MGMT2"])
   AC_SUBST([ink_with_modules_local],["-DLOCAL_MANAGER"])
   AC_SUBST([ink_with_modules_process],["-DPROCESS_MANAGER"])
   AC_DEFINE([MODULARIZED])
  ]
)
AM_CONDITIONAL([STANDALONE_IOCORE], [test "x$enable_standalone_iocore" = "xyes"])

#
# Remote Coverity Prevent commit
#

AC_MSG_CHECKING([whether to commit cov defects to remote host])
AC_ARG_ENABLE([remote-cov-commit],
  [AS_HELP_STRING([--enable-remote-cov-commit[=HOST]], [commit cov defects to remote host [HOST=localhost]])],
  [],
  [enable_remote_cov_commit=localhost]
)
AC_MSG_RESULT([$enable_remote_cov_commit])
AC_SUBST([enable_remote_cov_commit])

#
# API
#

AC_MSG_CHECKING([whether to enable API and plugin support])
AC_ARG_ENABLE([api],
  [AS_HELP_STRING([--disable-api],[do not enable API and plugin support])],
  [],
  [enable_api=yes]
)
AC_MSG_RESULT([$enable_api])
AS_IF([test "x$enable_api" = "xno"],
  [AC_DEFINE([INK_NO_TRANSFORM])
   AC_DEFINE([INK_NO_API])
   AC_DEFINE([INK_SINGLE_THREADED])
  ]
)

#
# Cli2
#

AC_MSG_CHECKING([whether to enable cli2])
AC_ARG_ENABLE([cli2],
  [AS_HELP_STRING([--disable-cli2],[do not compile cli2])],
  [],
  [enable_cli2=yes]
)
AC_MSG_RESULT([$enable_cli2])
AS_IF([test "x$enable_cli2" = "xyes"],
  [AC_SUBST([CLI_DIR],[cli2])]
)

AC_ARG_WITH([architecture],
  [AS_HELP_STRING([--with-architecture], [explicitly specify architecture for compiler])],
  [with_architecture=$withval],
  [with_architecture=no]
)
AS_IF([test "x$with_architecture" = "xyes"], [echo "--with-architecture needs an argument"; AS_EXIT()]
)
          
#
# Installation directories
#

# default: /usr/local/var/log
AC_SUBST([logdir],["$localstatedir/log"])

# default: /usr/local/var/log/trafficserver
AC_SUBST([pkglogdir],["$logdir/$PACKAGE_TARNAME"])

# default: /usr/local/var/trafficserver
AC_SUBST([pkglocalstatedir],["$localstatedir/$PACKAGE_TARNAME"])

# default: /usr/local/etc/trafficserver
AC_SUBST([pkgsysconfdir],["$sysconfdir/$PACKAGE_TARNAME"])

# default: /usr/local/libexec/trafficserver
AC_SUBST([pkglibexecdir],["$libexecdir/$PACKAGE_TARNAME"])

# -----------------------------------------------------------------------------
# 3. CHECK FOR PROGRAMS

# Compiler selection:
#
# Implementation note (toc)
# 1) Get default compiler settings (case statement.)
# 2) Check for over-rides of default compiler.
# 3) Set standard CFLAGS, SHARED_CFLAGS, etc.
# 4) (in first kludge mode block...) obtain any further CFLAG-type additions.
# 5) Test compilers with all flags set.

# These are shortcuts used in combination for the compiler options below
common_opt="-pipe -Wall -Werror -Wno-char-subscripts -Wno-write-strings"
debug_opt="-ggdb3 $common_opt"
release_opt="-g $common_opt -O3 -feliminate-unused-debug-symbols -fno-strict-aliasing"
cxx_opt="-Wno-invalid-offsetof"

cc_oflag_opt=$release_opt
cc_oflag_dbg=$debug_opt
cxx_oflag_opt="$release_opt $cxx_opt"
cxx_oflag_dbg="$debug_opt $cxx_opt"

CC=gcc
CXX=g++

SHARED_CFLAGS=-fPIC
SHARED_LDFLAGS=-shared
SHARED_CXXFLAGS=-fPIC
SHARED_CXXLINKFLAGS=-shared

# AC_PROG can sometimes mangle CFLAGS etc.
# in particular, on Linux they insert -g -O2, here we preserve any user CFLAGS

REAL_CFLAGS="${CFLAGS}"
REAL_CXXFLAGS="${CXXFLAGS}"
REAL_CCASFLAGS="${CCASFLAGS}"

AM_PROG_AS
AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_RANLIB
AC_PROG_INSTALL

CFLAGS="${REAL_CFLAGS}"
CXXFLAGS="${REAL_CXXFLAGS}"
CCASFLAGS="${REAL_CCASFLAGS}"

AC_PATH_PROG([DOXYGEN], [doxygen]) # needed for Doxygen
AC_PATH_PROG([PERL],[perl],[not found])
AS_IF([test "x$PERL" = "xnot found"],
  [AC_MSG_ERROR([check for perl failed. Have you installed perl?])]
)
AC_ARG_VAR([DOXYGEN], [full path of Doxygen executable])
AC_ARG_VAR([PERL], [full path of Perl executable])

AC_CHECK_PROG([CCACHE],[ccache],[ccache],[])

#
# Here are all the extra linux-specific C(XX)FLAGS additions and
# so forth.
# TODO cpu architecture settings separate from operating system settings
# 

# Examine the current CXXFLAGS / CFLAGS for patterns we might need
# ToDo: This needs to be made conditional on compiler used (only gcc is supported here)
AS_IF([test "x$with_architecture" != "xno"],
  [CFLAGS="$CFLAGS -march=$with_architecture"; CXXFLAGS="$CXXFLAGS -march=$with_architecture"]
)

AC_CHECK_SIZEOF(int *) # test for 64-bit pointers, this permits CFLAGS=-m32 for 32-bit compiles on 64-bit systems

# add a default march if there is not one set
if test "${ac_cv_sizeof_int_p}" = "4"; then
    case $CFLAGS in
  *\-march=*) : ;;
  *) CFLAGS="$CFLAGS -march=i586" ;;
esac

    case $CXXFLAGS in
  *\-march=*) : ;;
  *) CXXFLAGS="$CXXFLAGS -march=i586" ;;
esac
fi

AC_CACHE_CHECK([for gcc atomic builtins],[support_cv_atomic],
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],[[
int i, x =0;
i = __sync_add_and_fetch(&x,1);
return x;
]])],[support_cv_atomic="yes"],[support_cv_atomic="no"])])
AM_CONDITIONAL([SUPPORT_ATOMICS],[test x$support_cv_atomic = xyes])

# Feature Test Macros for the GNU C Library
# http://www.gnu.org/s/libc/manual/html_node/Feature-Test-Macros.html
AC_DEFINE([_FILE_OFFSET_BITS],64,
  [Determines which file system interface shall be used.]
)
AC_DEFINE([_GNU_SOURCE],1,
  [All extensions included: ISO C89, ISO C99, POSIX.1, POSIX.2, BSD, SVID, X/Open, LFS, and GNU extensions.]
)
AC_DEFINE([_REENTRANT],1,
  [Reentrant version of several functions get declared.]
)

EXTRA_CXX_LDFLAGS="-rdynamic"

    #
    # _Here_ is where we go ahead and add the _optimizations_ to already
    #  existing CFLAGS/CXXFLAGS if some special values had been set.
    #
if test "x${enable_debug}" = "xyes" ;then
    CFLAGS="${cc_oflag_dbg} ${CFLAGS}"
    CXXFLAGS="${cxx_oflag_dbg} ${CXXFLAGS}"
else
    CFLAGS="${cc_oflag_opt} ${CFLAGS}"
    CXXFLAGS="${cxx_oflag_opt} ${CXXFLAGS}"
fi

    #
    # Note:  These are site-specific macro's that do various tests
    #         on the selected compilers.  There was some tunning
    #         associated with our not wanting to use GNU for _everything_.
    # Note:  This macro may set certain parameters when run.
    #

AC_PROG_CC
AC_PROG_CXX
CC="$CCACHE $CC"
CXX="$CCACHE $CXX"

AC_PROG_CPP
AC_PROG_CXXCPP

# -----------------------------------------------------------------------------
# 4. CHECK FOR LIBRARIES

AC_CHECK_LIB([m],[sin],[AC_SUBST([LIBM],["-lm"])])
AC_CHECK_LIB([regex-3.6],[regcomp],[AC_SUBST([LIBREGEX],["-lregex-3.6"])])
AC_CHECK_LIB([exc],[exc_capture_context],[AC_SUBST([LIBEXC],["-lexc"])])
AC_CHECK_LIB([mld],[MLD_demangle_string],[AC_SUBST([LIBMLD],["-lmld"])])
AC_CHECK_LIB([crypt],[crypt],[AC_SUBST([LIBCRYPT],["-lcrypt"])])
AC_CHECK_LIB([dl],[dlopen],[AC_SUBST([LIBDL],["-ldl"])])
AC_CHECK_LIB([xml],[XML_SetUserData],[AC_SUBST([LIBXML],["-lxml"])])
AC_CHECK_LIB([socket],[socket],[AC_SUBST([LIBSOCKET],["-lsocket"])])
AC_CHECK_LIB([nsl],[gethostbyname],[AC_SUBST([LIBNSL],["-lnsl"])])
AC_CHECK_LIB([resolv],[res_init],[AC_SUBST([LIBRESOLV],["-lresolv"])])
AC_CHECK_LIB([resolv],[__putlong],[AC_SUBST([LIBRESOLV],["-lresolv"])])
AC_CHECK_LIB([pthread],[pthread_exit],[AC_SUBST([LIBTHREAD],["-lpthread"])])
AC_CHECK_LIB([rt],[clock_gettime],[AC_SUBST([LIBRT],["-lrt"])])
AC_CHECK_LIB([posix4],[clock_gettime],[AC_SUBST([LIBRT],["-lposix4"])])

AC_CHECK_LIB([ssl],[SSL_CTX_new],
  [AC_SUBST([LIBSSL],["-lssl -lcrypto"])
   AC_DEFINE([HAVE_LIBSSL],[1],[Define if you have libssl])
  ],
  [AC_MSG_FAILURE([check for libssl failed. Have you installed openssl-devel?])],
  [-ldl -lcrypto -lgcc]
)

#
# Tcl macros provided by m4/tcl.m4
#

# this will error out if tclConfig.sh is not found
SC_PATH_TCLCONFIG

# if tclConfig.sh loads properly, assume libraries are there and working
SC_LOAD_TCLCONFIG

# expect tclConfig.sh to populate TCL_LIB_FLAG and TCL_INCLUDE_SPEC
AC_SUBST([LIBTCL],[$TCL_LIB_FLAG])
CPPFLAGS="$CPPFLAGS $TCL_INCLUDE_SPEC"

AC_CHECK_LIB([expat],[XML_SetUserData],
  [AC_SUBST([LIBEXPAT],["-lexpat"])],
  [AC_MSG_FAILURE([check for libexpat failed. Have you installed expat-devel?])],
  [$LIBTHREAD]
)

ts_have_db=0
AC_CHECK_LIB([db],[__db185_open],
  [LIBDB="-ldb"
   ts_have_db=1
  ],
   [AC_MSG_WARN([check for libdb failed. Have you installed db4-devel?])]
)

AC_CHECK_LIB([c],[dbopen],
  [LIBDB=""
  ts_have_db=1
  ])
AC_SUBST([LIBDB])
 
if test "$ts_have_db" = "0"; then
    AC_MSG_FAILURE([No DB interface found])
fi

AC_CHECK_FUNC(clock_gettime)


# For SunPro 5.2 - we need the demangle symbol for
#  ink_stack_trace.cc in libinktomi++.  Because this
#  library is part of the SunPro distribution, we need
#  check with the C++ compiler since we might be using
#  SunPro CC for the C++ compiler and gcc for the C compiler
AC_LANG_PUSH([C++])
AC_CHECK_LIB([demangle],[cplus_demangle],
  [LIBDEMANGLE="-ldemangle"
   AC_DEFINE([HAVE_DEMANGLE])
  ]
)
AC_SUBST([LIBDEMANGLE])
AC_LANG_POP

# -----------------------------------------------------------------------------
# 5. CHECK FOR HEADER FILES

AC_CHECK_HEADERS([sys/epoll.h sys/event.h machine/endian.h endian.h sys/sysinfo.h sys/sysctl.h sys/systeminfo.h netinet/ip.h sys/ioctl.h sys/byteorder.h sys/sockio.h sys/sysctl.h sys/sysinfo.h sys/systeminfo.h arpa/nameser_compat.h db_185.h db.h execinfo.h netdb.h], [], [])


sinclude(build-aux/apr_common.m4)
sinclude(build-aux/apr_network.m4)
APR_CHECK_GETHOSTBYNAME_R_STYLE

AC_CHECK_HEADERS([sys/epoll.h sys/event.h machine/endian.h endian.h sys/sysinfo.h sys/sysctl.h sys/systeminfo.h netinet/ip.h sys/ioctl.h sys/byteorder.h sys/sockio.h sys/sysctl.h sys/sysinfo.h sys/systeminfo.h arpa/nameser_compat.h], [], [])

#
# use modular IOCORE 
# 
iocore_include_dirs="-I\$(top_srcdir)/librecords \
 -I\$(top_srcdir)/iocore/utils \
 -I\$(top_srcdir)/iocore/cache \
 -I\$(top_srcdir)/iocore/net \
 -I\$(top_srcdir)/iocore/eventsystem \
 -I\$(top_srcdir)/iocore/aio \
 -I\$(top_srcdir)/iocore/dns \
 -I\$(top_srcdir)/iocore/hostdb \
 -I\$(top_srcdir)/iocore/cluster" 

AC_DEFINE([HTTP_CACHE])

AC_LIBOBJ([ink_pread_linux])

# Testing Framework suffix generation hack
TFW_PACKAGE_SUFFIX=$os_type

MGMT_DEFS="$MGMT_DEFS -DMGMT_USE_SYSLOG"

AC_SUBST([API_DEFS])
AC_SUBST([CC])
AC_SUBST([CFLAGS])
AC_SUBST([CXX])
AC_SUBST([CXXFLAGS])
AC_SUBST([EXPAT_LDFLAGS])
AC_SUBST([EXTRA_CC_LDFLAGS])
AC_SUBST([EXTRA_CXX_LDFLAGS])
AC_SUBST([iocore_include_dirs])
AC_SUBST([LDFLAGS])
AC_SUBST([MGMT_DEFS])
AC_SUBST([SHARED_CFLAGS])
AC_SUBST([SHARED_CXXFLAGS])
AC_SUBST([SHARED_CXXLINKFLAGS])
AC_SUBST([SHARED_LDFLAGS])
AC_SUBST([TFW_PACKAGE_SUFFIX])

# -----------------------------------------------------------------------------
# 6. OUTPUT FILES

AC_CONFIG_FILES([doc/Doxyfile])
AC_CONFIG_FILES([doc/Makefile])
AC_CONFIG_FILES([install/Makefile])
AC_CONFIG_FILES([iocore/aio/Makefile])
AC_CONFIG_FILES([iocore/cache/Makefile])
AC_CONFIG_FILES([iocore/cluster/Makefile])
AC_CONFIG_FILES([iocore/dns/Makefile])
AC_CONFIG_FILES([iocore/eventsystem/Makefile])
AC_CONFIG_FILES([iocore/hostdb/Makefile])
AC_CONFIG_FILES([iocore/Makefile])
AC_CONFIG_FILES([iocore/net/Makefile])
AC_CONFIG_FILES([iocore/utils/Makefile])
AC_CONFIG_FILES([libinktomi++/Makefile])
AC_CONFIG_FILES([librecords/Makefile])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([proxy/config/body_factory/default/Makefile])
AC_CONFIG_FILES([proxy/config/body_factory/Makefile])
AC_CONFIG_FILES([proxy/config/records.config])
AC_CONFIG_FILES([proxy/config/Makefile])
AC_CONFIG_FILES([proxy/congest/Makefile])
AC_CONFIG_FILES([proxy/dns_cache/Makefile])
AC_CONFIG_FILES([proxy/hdrs/Makefile])
AC_CONFIG_FILES([proxy/http2/Makefile])
AC_CONFIG_FILES([proxy/logging/Makefile])
AC_CONFIG_FILES([proxy/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/api2/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/api2/remote/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/cli2/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/cli/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/cluster/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/cop/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/preparse/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/stats/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/tools/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/utils/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/web2/Makefile])
AC_CONFIG_FILES([proxy/stats/Makefile])

AC_OUTPUT

