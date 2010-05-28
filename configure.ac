# Process this file with `autoreconf -i` to create a 'configure' file.

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

# Table of Contents
# 1. INITIALIZATION
# 2. SITE CONFIGURATION
# 3. CHECK FOR PROGRAMS
# 4. CHECK FOR LIBRARIES
# 5. CHECK FOR HEADERS
# 6. OUTPUT FILES
# 7. autoheader TEMPLATES

# -----------------------------------------------------------------------------
# 1. INITIALIZATION

# These first two version numbers are updated automatically on each release.
# Version number is calculated as MAJOR * 1000000 + MINOR * 1000 + MICRO
# Version string is in the form of MAJOR.MINOR.MICRO[sufix]
#
m4_define([ATS_VERSION_S],[2.1.1-unstable])
m4_define([ATS_VERSION_N],[2001001])

AC_INIT([Apache Traffic Server], ATS_VERSION_S(), [dev@trafficserver.apache.org], [trafficserver],[http://trafficserver.apache.org])
AC_PREREQ([2.59])
AC_CONFIG_AUX_DIR([build/aux])
AC_CONFIG_SRCDIR([proxy/Main.cc])
AC_CONFIG_MACRO_DIR([build])
AM_INIT_AUTOMAKE([tar-ustar -Wall -Werror foreign no-installinfo no-installman 1.9.2])
AM_CONFIG_HEADER([libinktomi++/ink_autoconf.h])

# Libtool versioning uses different conventions on different
# platforms.  At least on FreeBSD, libtool uses an overly complex
# convention that attempts to solve problems that most people just
# don't have and which just causes confusion for most end users.
#
ATS_VERSION_MAJOR=$((ATS_VERSION_N() / 1000000 ))
ATS_VERSION_MINOR=$(((ATS_VERSION_N() / 1000) % 1000 ))
ATS_VERSION_MICRO=$((ATS_VERSION_N() % 1000 ))
ATS_LIBTOOL_MAJOR=`echo $((${ATS_VERSION_MAJOR} + ${ATS_VERSION_MINOR}))`
ATS_LIBTOOL_VERSION=$ATS_LIBTOOL_MAJOR:$ATS_VERSION_MICRO:$ATS_VERSION_MINOR
ATS_VERSION_STRING=ATS_VERSION_S()
ATS_VERSION_NUMBER=ATS_VERSION_N()

#
# Substitute the above version numbers into the various files below.
#
AC_SUBST(ATS_LIBTOOL_VERSION)
AC_SUBST(ATS_VERSION_STRING)
AC_SUBST(ATS_VERSION_NUMBER)
AC_SUBST(ATS_VERSION_MAJOR)
AC_SUBST(ATS_VERSION_MINOR)
AC_SUBST(ATS_VERSION_MICRO)

dnl Hard-coded top of ink_autoconf.h:
AH_TOP([
#ifndef _ink_autoconf_h
#define _ink_autoconf_h
])

dnl Hard-coded inclusion at the tail end of ink_autoconf.h:
AH_BOTTOM([

#endif /* _ink_autoconf_h */
])

#
# Generate ./config.nice for reproducing runs of configure
#
ATS_CONFIG_NICE([config.nice])

# XXX we can't just use AC_PREFIX_DEFAULT because that isn't subbed in
# by configure until it is too late.  Is that how it should be or not?
# Something seems broken here.
AC_PREFIX_DEFAULT([/usr/local/trafficserver])

# Get the layout here, so we can pass the required variables to Trafficserver
ATS_ENABLE_LAYOUT(TrafficServer, [cachedir])

# Reparse the configure arguments so we can override the layout.
ATS_PARSE_ARGUMENTS

#
# Host detection
#
AC_CANONICAL_HOST

#
# Build environment
#
build_person="`id -nu`"
build_group="`id -ng`"
build_machine="`uname -n`"
AC_SUBST([build_machine])
AC_SUBST([build_person])
AC_SUBST([build_group])

AC_ARG_WITH([user],
  [AS_HELP_STRING([--with-user],[specify the system user])],
  [
  with_user="$withval"
  ],[
  with_user="nobody"
  ]
)

AC_ARG_WITH([group],
  [AS_HELP_STRING([--with-group],[specify the system group])],
  [
  with_group="$withval"
  ],[
  with_group="nobody"
  ]
)
AC_SUBST([pkgsysuser],[$with_user])
AC_SUBST([pkgsysgroup],[$with_group])

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

#
# Diags
#

use_diags=0
AC_MSG_CHECKING([whether to enable diags])
AC_ARG_ENABLE([diags],
  [AS_HELP_STRING([--disable-diags],[turn off diags])],
  [],
  [enable_diags="yes"
   use_diags=1
  ]
)
AC_SUBST(use_diags)

#
# Purify
#
has_purify=0
AC_MSG_CHECKING([whether to enable purify])
AC_ARG_ENABLE([purify],
  [AS_HELP_STRING([--enable-purify],[enable support for Purify])],
  [has_purify=1],
  [enable_purify=no]
)
AC_MSG_RESULT([$enable_purify])
AC_SUBST(has_purify)

#
# libev
#

AC_MSG_CHECKING([whether to use libev even if not required])
AC_ARG_ENABLE([libev],
  [AS_HELP_STRING([--enable-libev],[use libev even if not required])],
  [],
  [enable_libev=no]
)
AC_MSG_RESULT([$enable_libev])


#
# Micro
#
is_micro_build=0
AC_MSG_CHECKING([whether to enable micro-TS])
AC_ARG_ENABLE([micro],
  [AS_HELP_STRING([--enable-micro],[enable micro-TS build])],
  [is_micro_build=1],
  [enable_micro=no]
)
AC_SUBST(is_micro_build)
AC_MSG_RESULT([$enable_micro])

#
# Standalone iocore
#
has_standalone_iocore=0
AC_MSG_CHECKING([whether to build a standalone iocore])
AC_ARG_ENABLE([standalone-iocore],
  [AS_HELP_STRING([--enable-standalone-iocore],[build just standalone iocore])],
  [has_standalone_iocore=1],
  [enable_standalone_iocore=no]
)
AC_MSG_RESULT([$enable_standalone_iocore])
AC_SUBST(has_standalone_iocore)
AS_IF([test "x$enable_standalone_iocore" = "xyes"],
  [AC_SUBST([IOCORE_MODULARIZED_DEFS],[""])
   AC_SUBST([ink_with_modules_def],["-DREC_BUILD_STAND_ALONE"])
   AC_SUBST([ink_with_modules_local],[""])
   AC_SUBST([ink_with_modules_process],[""])
   AC_SUBST([hdr_dirs],"proxy/hdrs")
  ],
  [AC_SUBST([ink_with_modules_def],["-DREC_BUILD_MGMT2"])
   AC_SUBST([ink_with_modules_local],["-DLOCAL_MANAGER"])
   AC_SUBST([ink_with_modules_process],["-DPROCESS_MANAGER"])
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
# WebUI
#
has_webui=0
AC_MSG_CHECKING([whether to enable Web UI])
AC_ARG_ENABLE([webui],
  [AS_HELP_STRING([--enable-webui],[enable Web UI])],
  [has_webui=1],
  [enable_webui=no]
)
AC_MSG_RESULT([$enable_webui])
AC_SUBST(has_webui)

#
# API
#
has_inkapi=1
AC_MSG_CHECKING([whether to enable API and plugin support])
AC_ARG_ENABLE([api],
  [AS_HELP_STRING([--disable-api],[do not enable API and plugin support])],
  [has_inkapi=0],
  [enable_api=yes]
)
AC_MSG_RESULT([$enable_api])
AC_SUBST(has_inkapi)

#
# Cli2
#
has_cli2=1
AC_MSG_CHECKING([whether to enable cli2])
AC_ARG_ENABLE([cli2],
  [AS_HELP_STRING([--disable-cli2],[do not compile cli2])],
  [has_cli2=0],
  [enable_cli2=yes]
)
AC_MSG_RESULT([$enable_cli2])
AS_IF([test "x$enable_cli2" = "xyes"],
  [AC_SUBST([CLI_DIR],[cli2])]
)
AC_SUBST(has_cli2)

# Google profiler
AC_MSG_CHECKING([whether to enable profiler])
AC_ARG_WITH([profiler],
  [AS_HELP_STRING([--with-profiler],[enable support for profiler [default=no]])],
  [with_profiler=$withval],
  [with_profiler=no]
)
AC_MSG_RESULT([$with_profiler])

#
# use eventfd() or pipes
# Found that ec2 is using an older kernel causing eventfd errors.
# Disable eventfd when using ATS on EC2 Fedora.
#
AC_MSG_CHECKING([whether to enable eventfd()])
AC_ARG_ENABLE([eventfd],
  [AS_HELP_STRING([--disable-eventfd],[turn off eventfd and use pipes])],
  [],
  [enable_eventfd="yes"]
)
AC_MSG_RESULT([$enable_eventfd])

#
# Installation directories
# For each var the following is evaluated
# foo      Standard variable  eg. ${prefix}/foo
# rel_foo  Relative to prefix eg. foo
#
ATS_SUBST_LAYOUT_PATH([prefix])
ATS_SUBST_LAYOUT_PATH([exec_prefix])
ATS_SUBST_LAYOUT_PATH([bindir])
ATS_SUBST_LAYOUT_PATH([sbindir])
ATS_SUBST_LAYOUT_PATH([libdir])
ATS_SUBST_LAYOUT_PATH([libexecdir])
ATS_SUBST_LAYOUT_PATH([infodir])
ATS_SUBST_LAYOUT_PATH([mandir])
ATS_SUBST_LAYOUT_PATH([sysconfdir])
ATS_SUBST_LAYOUT_PATH([datadir])
ATS_SUBST_LAYOUT_PATH([installbuilddir])
ATS_SUBST_LAYOUT_PATH([includedir])
ATS_SUBST_LAYOUT_PATH([localstatedir])
ATS_SUBST_LAYOUT_PATH([runtimedir])
ATS_SUBST_LAYOUT_PATH([logdir])
ATS_SUBST_LAYOUT_PATH([cachedir])

ATS_SUBST([pkgbindir])
ATS_SUBST([pkgsbindir])
ATS_SUBST([pkglibdir])
ATS_SUBST([pkglibexecdir])
ATS_SUBST([pkgsysconfdir])
ATS_SUBST([pkgdatadir])
ATS_SUBST([pkglocalstatedir])
ATS_SUBST([pkgruntimedir])
ATS_SUBST([pkglogdir])
ATS_SUBST([pkgcachedir])


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

# AC_PROG can sometimes mangle CFLAGS etc.
# in particular, on Linux they insert -g -O2, here we preserve any user CFLAGS

#CC=gcc
#CXX=g++

REAL_CFLAGS="${CFLAGS}"
REAL_CXXFLAGS="${CXXFLAGS}"
REAL_CCASFLAGS="${CCASFLAGS}"

AC_CHECK_PROG([CCACHE],[ccache],[ccache],[])
AC_PROG_CC
AC_PROG_CXX
if test "x${CCACHE}" = "xccache" ;then
 CC="$CCACHE $CC"
 CXX="$CCACHE $CXX"
fi
dnl AC_PROG_SED is only avaliable in recent autoconf versions.
dnl Use AC_CHECK_PROG instead if AC_PROG_SED is not present.
ifdef([AC_PROG_SED],
      [AC_PROG_SED],
      [AC_CHECK_PROG(SED, sed, sed)])

AC_PROG_CPP
AC_PROG_CXXCPP
AM_PROG_AS
AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_CHECK_PROG(RM, rm, rm)
AC_CHECK_PROG(ASCPP, cpp, cpp)
AC_CHECK_TOOL(AR, ar, ar)
AC_ISC_POSIX

AC_PATH_PROG([DOXYGEN], [doxygen]) # needed for Doxygen
AC_PATH_PROG([PERL],[perl],[not found])
AS_IF([test "x$PERL" = "xnot found"],
  [AC_MSG_ERROR([check for perl failed. Have you installed perl?])]
)
AC_ARG_VAR([DOXYGEN], [full path of Doxygen executable])
AC_ARG_VAR([PERL], [full path of Perl executable])

CFLAGS="${REAL_CFLAGS}"
CXXFLAGS="${REAL_CXXFLAGS}"
CCASFLAGS="${REAL_CCASFLAGS}"

base_cc=`basename $CC`
# These are shortcuts used in combination for the compiler options below
case $host_os in
  linux*)
    common_opt="-pipe -Wall -Werror"
    debug_opt="-ggdb3 $common_opt"
    release_opt="-g $common_opt -O3 -feliminate-unused-debug-symbols -fno-strict-aliasing"
    cxx_opt="-Wno-invalid-offsetof"
    ;;
  darwin*)
    common_opt="-pipe -Wall -Werror"
    debug_opt="-ggdb3 $common_opt"
    release_opt="-g $common_opt -O3 -feliminate-unused-debug-symbols -fno-strict-aliasing"
    cxx_opt="-Wno-invalid-offsetof"
    ATS_ADDTO(CPPFLAGS, [-I/opt/local/include])
    ATS_ADDTO(LDFLAGS, [-L/opt/local/lib])
    ;;
  freebsd*)
    common_opt="-pipe -Wall -Werror"
    debug_opt="-ggdb3 $common_opt"
    release_opt="-g $common_opt -O3 -feliminate-unused-debug-symbols -fno-strict-aliasing"
    cxx_opt="-Wno-invalid-offsetof"
    ATS_ADDTO(LDFLAGS, [-L/usr/local/lib])
    ;;
  solaris*)
    if test "x${base_cc}" = "xcc" ;then
      common_opt="-mt -m64 -D__WORDSIZE=64" # FIXME: arch should be detected
      CCASFLAGS="-Wa,-32" # TODO: add 64bit support to atomic ops
      debug_opt="-g $common_opt"
      release_opt="-g $common_opt -xO3"
      cxx_opt="-library=stlport4 -erroff"
      AC_DEFINE([_POSIX_PTHREAD_SEMANTICS],1,
       [posix thread semantics]
      )
    else # gcc
      common_opt="-pipe -Wall -Werror"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt -O3 -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
    fi
    ATS_ADDTO(LDFLAGS, [-L/lib])
    ATS_ADDTO(LDFLAGS, [-L/usr/local/lib])
    ;;
  *)
    common_opt="-pipe -Wall -Werror"
    debug_opt="-ggdb3 $common_opt"
    release_opt="-g $common_opt -O3 -feliminate-unused-debug-symbols -fno-strict-aliasing"
    cxx_opt="-Wno-invalid-offsetof"
    ;;
esac

cc_oflag_opt=$release_opt
cc_oflag_dbg=$debug_opt
cxx_oflag_opt="$release_opt $cxx_opt"
cxx_oflag_dbg="$debug_opt $cxx_opt"

SHARED_CFLAGS=-fPIC
SHARED_LDFLAGS=-shared
SHARED_CXXFLAGS=-fPIC
SHARED_CXXLINKFLAGS=-shared

dnl Checks for pointer size
AC_CHECK_SIZEOF(void*, 4)
if test "x$ac_cv_sizeof_voidp" == "x"; then
  AC_ERROR([Cannot determine size of void*])
fi
AC_SUBST(ac_cv_sizeof_voidp)

#
# Here are all the extra linux-specific C(XX)FLAGS additions and
# so forth.
# TODO cpu architecture settings separate from operating system settings
#
cpu_architecture=""
# GCC: add a default march if there is not one set
if test "x${GCC}" = "xyes"; then
  if test "${ac_cv_sizeof_voidp}" = "4"; then
    case "$host_cpu" in
      i?86* | k[5-8]* | pentium* | athlon)
        cpu_architecture="-march=i586"
      ;;
      *sparc*)
        cpu_architecture="-mv8"
      ;;
    esac
  else
    case "$host_cpu" in
      x86_64 | amd64)
        # XXX: Any need for 64-bit arch flags?
        # cpu_architecture="-march=native"
      ;;
      *sparc*)
        cpu_architecture="-march=ultrasparc"
      ;;
    esac
  fi
  # TODO: Add support for other compilers
  #
fi

# Overrride detected architecture with the user suplied one
#
AC_ARG_WITH(architecture, [AC_HELP_STRING([--with-architecture=ARCH],[use a specific CPU architecture])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "xno"; then
    case "$withval" in
      -*)
        # TODO: In case we are cross compiling some of the provided flags
        #       should be added to the LDFLAGS
        cpu_architecture="$withval"
      ;;
      *)
        cpu_architecture="-march=$withval"
      ;;
    esac
  elif test "x$withval" = "x"; then
    AC_MSG_ERROR([--with-architecture requires an param])
  fi
])

if test "x$cpu_architecture" != "x"; then
  ATS_ADDTO(CFLAGS, [$cpu_architecture])
  ATS_ADDTO(CXXFLAGS, [$cpu_architecture])
fi

# GCC atomics
AC_CACHE_CHECK([for gcc atomic builtins],[support_cv_atomic],
[AC_LINK_IFELSE([AC_LANG_PROGRAM([],[[
int i, x =0;
i = __sync_add_and_fetch(&x,1);
return x;
]])],[support_cv_atomic="yes"],[support_cv_atomic="no"])])
AM_CONDITIONAL([SUPPORT_ATOMICS],[test x$support_cv_atomic = xyes])

# 64-bit LFS support
#
ATS_ADDTO(CPPFLAGS, [-D_LARGEFILE64_SOURCE=1])
if test "${ac_cv_sizeof_voidp}" = "8"; then
  ATS_ADDTO(CPPFLAGS, [-D_COMPILE64BIT_SOURCE=1])
else
  ATS_ADDTO(CPPFLAGS, [-D_FILE_OFFSET_BITS=64])
fi
ATS_ADDTO(CPPFLAGS, [-D_GNU_SOURCE])
ATS_ADDTO(CPPFLAGS, [-D_REENTRANT])

case $host_os in
  linux*)
    EXTRA_CXX_LDFLAGS="-rdynamic"
    host_os_def=linux
    ;;
  darwin*)
    host_os_def=darwin
    ;;
  freebsd*)
    EXTRA_CXX_LDFLAGS="-rdynamic"
    host_os_def=freebsd
    ;;
  solaris*)
    host_os_def=solaris
    ;;
  *)
    EXTRA_CXX_LDFLAGS="-rdynamic"
    host_os_def=unknown
   ;;
esac
ATS_ADDTO(CPPFLAGS, [-D$host_os_def])

#
# _Here_ is where we go ahead and add the _optimizations_ to already
#  existing CFLAGS/CXXFLAGS if some special values had been set.
#
if test "x${enable_debug}" = "xyes" ;then
  ATS_ADDTO(CFLAGS, [${cc_oflag_dbg}])
  ATS_ADDTO(CXXFLAGS, [${cxx_oflag_dbg}])
  ATS_ADDTO(CPPFLAGS, [-DDEBUG -D_DEBUG])
else
  ATS_ADDTO(CFLAGS, [${cc_oflag_opt}])
  ATS_ADDTO(CXXFLAGS, [${cxx_oflag_opt}])
fi

#
# Note:  These are site-specific macro's that do various tests
#         on the selected compilers.  There was some tunning
#         associated with our not wanting to use GNU for _everything_.
# Note:  This macro may set certain parameters when run.
#


# -----------------------------------------------------------------------------
# 4. CHECK FOR LIBRARIES

AC_CHECK_LIB([m],[sin],[AC_SUBST([LIBM],["-lm"])])
AC_CHECK_LIB([exc],[exc_capture_context],[AC_SUBST([LIBEXC],["-lexc"])])
AC_CHECK_LIB([mld],[MLD_demangle_string],[AC_SUBST([LIBMLD],["-lmld"])])
AC_CHECK_LIB([dl],[dlopen],[AC_SUBST([LIBDL],["-ldl"])])
AC_CHECK_LIB([socket],[socket],[AC_SUBST([LIBSOCKET],["-lsocket"])])
AC_CHECK_LIB([nsl],[gethostbyname],[AC_SUBST([LIBNSL],["-lnsl"])])
AC_CHECK_LIB([resolv],[res_init],[AC_SUBST([LIBRESOLV],["-lresolv"])])
AC_CHECK_LIB([resolv],[__putlong],[AC_SUBST([LIBRESOLV],["-lresolv"])])
AC_CHECK_LIB([pthread],[pthread_exit],[AC_SUBST([LIBTHREAD],["-lpthread"])])
AC_CHECK_LIB([rt],[clock_gettime],[AC_SUBST([LIBRT],["-lrt"])])
AC_CHECK_LIB([posix4],[clock_gettime],[AC_SUBST([LIBRT],["-lposix4"])])
AC_CHECK_LIB([iconv],[iconv_open],[AC_SUBST([LIBICONV],["-liconv"])])
AC_CHECK_LIB([iconv],[libiconv_open],[AC_SUBST([LIBICONV],["-liconv"])])
# TODO: We have --enable-libev but here we add it unconditionally
#       making resulting binaries always linked to libev if present.
#       Use a proper --with-libev and fail on --enable-libev and -lev
#       is missing. Allow --with-libev=builtin? living in ./srclib/ev ?
#
AC_CHECK_LIB([ev],[ev_sleep],[AC_SUBST([LIBEV],["-lev"])])

#
# Check for SSL presence and usability
ATS_CHECK_CRYPTO
if test "x${enable_crypto}" != "xyes"; then
  AC_MSG_ERROR([Need at least one SSL library, --with-openssl is supported])
fi

#
# Check for zlib presence and usability
ATS_CHECK_ZLIB

#
# Check for lzma presence and usability
ATS_CHECK_LZMA

#
# Tcl macros provided by m4/tcl.m4
#
# this will error out if tclConfig.sh is not found
SC_PATH_TCLCONFIG

# if tclConfig.sh loads properly, assume libraries are there and working
SC_LOAD_TCLCONFIG

# expect tclConfig.sh to populate TCL_LIB_FLAG and TCL_INCLUDE_SPEC
case $host_os in
  darwin*)
    TCL_LIB_FLAG="-ltcl" # OSX fails to populate this variable
    ;;
  *)
   ;;
esac
AC_SUBST([LIBTCL],[$TCL_LIB_FLAG])

case $host_os in
  freebsd*)
    ATS_ADDTO(CPPFLAGS, [-I/usr/local/include])
    ;;
  solaris*)
    ATS_ADDTO(CPPFLAGS, [-I/usr/local/include])
    ;;
esac
ATS_ADDTO(CPPFLAGS, [$TCL_INCLUDE_SPEC])

#
# Check for XML parser
#
ATS_CHECK_XML
if test "x${enable_xml}" != "xyes"; then
  AC_MSG_ERROR([Need at least one XML library, --with-expat is supported])
fi

#
# Check for default sqlite3 DB backend
#
use_sqlite3=1
use_libdb=0
ATS_CHECK_SQLITE3
if test "x${enable_sqlite3}" != "xyes"; then
  ATS_CHECK_BDB
  if test "x${enable_libdb}" != "xyes"; then
    AC_MSG_ERROR([Need at least one DB backend, --with-sqlite3 and --with-libdb are supported])
  fi
  use_sqlite3=0
  use_libdb=1
fi
AC_SUBST(use_sqlite3)
AC_SUBST(use_libdb)

ATS_FLAG_FUNCS([clock_gettime kqueue epoll_ctl posix_memalign posix_fadvise lrand48_r srand48_r port_create])
ATS_FLAG_FUNCS([strndup strlcpy strlcat])

AC_SUBST(has_clock_gettime)
AC_SUBST(has_posix_memalign)
AC_SUBST(has_posix_fadvise)
AC_SUBST(has_lrand48_r)
AC_SUBST(has_srand48_r)
AC_SUBST(has_strndup)
AC_SUBST(has_strlcpy)
AC_SUBST(has_strlcat)

has_eventfd=0
AS_IF([test "x$enable_eventfd" = "xyes"],
  [ATS_FLAG_FUNCS([eventfd])]
)
AC_SUBST(has_eventfd)

#
# Check for pcre library
#
ATS_CHECK_PCRE
if test "x${enable_pcre}" != "xyes"; then
  AC_MSG_ERROR([Cannot find pcre library. Configure --with-pcre=DIR])
fi

has_backtrace=0
# Check for backtrace() support
ATS_FLAG_HEADERS([execinfo.h], [has_backtrace=1],[])
if test "${has_backtrace}" = "1"; then
  # FreeBSD requires '/usr/ports/devel/libexecinfo' for gdb style backtrace() support
  AC_CHECK_LIB([execinfo], [backtrace], [have_backtrace_lib=yes
                                         AC_SUBST([LIBEXECINFO],["-lexecinfo"])])
  if test "x${have_backtrace_lib}" = "xyes"; then
    AC_MSG_NOTICE([Using backtrace library '-lexecinfo'])
  fi
else
  AC_MSG_WARN([No backtrace() support found])
fi
AC_SUBST(execinfoh)
AC_SUBST(has_backtrace)

use_libev=0
use_epoll=0
use_kqueue=0
use_port=0
if test "x$enable_libev" = "xyes"; then
  use_libev=1
  have_good_poller=1
elif test "$ac_cv_func_epoll_ctl" = "yes"; then
  use_epoll=1
  have_good_poller=1
  AC_MSG_NOTICE([Using epoll event interface])
elif test "$ac_cv_func_kqueue" = "yes"; then
  use_kqueue=1
  have_good_poller=1
  AC_MSG_NOTICE([Using kqueue event interface])
elif test "$ac_cv_func_port_create" = "yes"; then
  use_port=1
  have_good_poller=1
  AC_MSG_NOTICE([Using port event interface])
else
  AC_MSG_FAILURE([No suitable polling interface found])
fi
AC_SUBST(use_libev)
AC_SUBST(use_epoll)
AC_SUBST(use_kqueue)
AC_SUBST(use_port)

has_profiler=0
if test "x${with_profiler}" = "xyes"; then
  AC_CHECK_LIB([profiler], [ProfilerStart],
    [AC_SUBST([LIBPROFILER], ["-lprofiler"])
     has_profiler=1
    ],
    [AC_MSG_FAILURE([check for profiler failed. Have you installed google-perftools-devel?])],
  )
fi
AC_SUBST(has_profiler)

has_demangle=0
# For SunPro 5.2 - we need the demangle symbol for
#  ink_stack_trace.cc in libinktomi++.  Because this
#  library is part of the SunPro distribution, we need
#  check with the C++ compiler since we might be using
#  SunPro CC for the C++ compiler and gcc for the C compiler
AC_LANG_PUSH([C++])
AC_CHECK_LIB([demangle],[cplus_demangle],
  [LIBDEMANGLE="-ldemangle"
   has_demangle=1
  ]
)
AC_SUBST([LIBDEMANGLE])
AC_LANG_POP
AC_SUBST(has_demangle)

need_union_semun=0
# It's stupid, but not all platforms have union semun, even those that need it.
AC_MSG_CHECKING(for union semun in sys/sem.h)
AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
],[
union semun arg;
semctl(0, 0, 0, arg);
], [union_semun=yes]
msg=yes, [
need_union_semun="1"
msg=no ] )
AC_MSG_RESULT([$msg])

AC_SUBST(need_union_semun)

# -----------------------------------------------------------------------------
# 5. CHECK FOR HEADER FILES

ATS_FLAG_HEADERS([sys/epoll.h \
                  sys/event.h \
                  machine/endian.h \
                  endian.h \
                  sys/sysinfo.h \
                  sys/sysctl.h \
                  sys/systeminfo.h \
                  netinet/in.h \
                  netinet/in_systm.h \
                  netinet/tcp.h \
                  sys/ioctl.h \
                  sys/byteorder.h \
                  sys/sockio.h \
                  arpa/inet.h \
                  arpa/nameser.h \
                  arpa/nameser_compat.h \
                  execinfo.h \
                  netdb.h \
                  ctype.h \
                  siginfo.h \
                  malloc.h \
                  wait.h \
                  float.h \
                  libgen.h \
                  values.h \
                  alloca.h \
                  cpio.h \
                  stropts.h \
                  sys/mount.h \
                  sys/param.h \
                  sys/sysmacros.h \
                  math.h \
                  stdint.h \
                  net/ppp_defs.h])

AC_SUBST(sys_epollh)
AC_SUBST(sys_eventh)
AC_SUBST(machine_endianh)
AC_SUBST(endianh)
AC_SUBST(netinet_inh)
AC_SUBST(netinet_in_systmh)
AC_SUBST(netinet_tcph)
AC_SUBST(sys_ioctlh)
AC_SUBST(sys_byteorderh)
AC_SUBST(sys_sockioh)
AC_SUBST(sys_sysctlh)
AC_SUBST(sys_sysinfoh)
AC_SUBST(sys_systeminfoh)
AC_SUBST(arpa_ineth)
AC_SUBST(arpa_nameserh)
AC_SUBST(arpa_nameser_compath)
AC_SUBST(execinfoh)
AC_SUBST(netdbh)
AC_SUBST(ctypeh)

AC_SUBST(siginfoh)
AC_SUBST(malloch)
AC_SUBST(waith)
AC_SUBST(floath)
AC_SUBST(libgenh)
AC_SUBST(valuesh)
AC_SUBST(allocah)
AC_SUBST(cpioh)
AC_SUBST(stroptsh)
AC_SUBST(sys_mounth)
AC_SUBST(sys_paramh)
AC_SUBST(sys_sysmacrosh)
AC_SUBST(mathh)
AC_SUBST(net_ppp_defsh)

ATS_FLAG_HEADERS([netinet/ip.h], [], [],
                 [[#ifdef HAVE_SYS_TYPES_H
                    #include <sys/types.h>
                   #endif
                   #ifdef HAVE_NETINET_IN_H
                    #include <netinet/in.h>
                   #endif
                 ]])

ATS_FLAG_HEADERS([netinet/ip_icmp.h], [], [],
                 [[#ifdef HAVE_SYS_TYPES_H
                    #include <sys/types.h>
                   #endif
                   #ifdef HAVE_NETINET_IN_H
                    #include <netinet/in.h>
                   #endif
                   #ifdef HAVE_NETINET_IP_H
                    #include <netinet/ip.h>
                   #endif
                   #ifdef HAVE_NETINET_IN_SYSTM_H
                    #include <netinet/in_systm.h>
                   #endif
                 ]])

AC_SUBST(netinet_iph)
AC_SUBST(netinet_ip_icmph)

if test "x${with_profiler}" = "xyes"; then
ATS_FLAG_HEADERS([google/profiler.h \
                  ], [], [])
fi

ATS_CHECK_DEFAULT_IFACE
ATS_CHECK_GETHOSTBYNAME_R_STYLE

#
# use modular IOCORE
#
iocore_include_dirs="-I\$(top_srcdir)/librecords \
 -I\$(top_srcdir)/libinktomi++ \
 -I\$(top_srcdir)/iocore/eventsystem \
 -I\$(top_srcdir)/iocore/net \
 -I\$(top_srcdir)/iocore/aio \
 -I\$(top_srcdir)/iocore/hostdb \
 -I\$(top_srcdir)/iocore/cache \
 -I\$(top_srcdir)/iocore/cluster \
 -I\$(top_srcdir)/iocore/utils \
 -I\$(top_srcdir)/iocore/dns \
 -I\$(top_srcdir)/libev"


# Testing Framework suffix generation hack
TFW_PACKAGE_SUFFIX=$os_type

MGMT_DEFS="$MGMT_DEFS -DMGMT_USE_SYSLOG"

AC_MSG_NOTICE([Build using CC=$CC])
AC_MSG_NOTICE([Build using CXX=$CXX])
AC_MSG_NOTICE([Build using CPP=$CPP])
AC_MSG_NOTICE([Build using CCAS=$CCAS])
AC_MSG_NOTICE([Build using CFLAGS=$CFLAGS])
AC_MSG_NOTICE([Build using SHARED_CFLAGS=$SHARED_CFLAGS])
AC_MSG_NOTICE([Build using CXXFLAGS=$CXXFLAGS])
AC_MSG_NOTICE([Build using SHARED_CXXFLAGS=$SHARED_CXXFLAGS])
AC_MSG_NOTICE([Build using SHARED_CXXLINKFLAGS=$SHARED_LINKCXXFLAGS])
AC_MSG_NOTICE([Build using CPPFLAGS=$CPPFLAGS])
AC_MSG_NOTICE([Build using CCASFLAGS=$CCASFLAGS])
AC_MSG_NOTICE([Build using LDFLAGS=$LDFLAGS])
AC_MSG_NOTICE([Build using SHARED_LDFLAGS=$SHARED_LDFLAGS])
AC_MSG_NOTICE([Build using EXTRA_CC_LDFLAGS=$EXTRA_CC_LDFLAGS])
AC_MSG_NOTICE([Build using EXTRA_CXX_LDFLAGS=$EXTRA_CXX_LDFLAGS])
AC_MSG_NOTICE([Build using MGMT_DEFS=$MGMT_DEFS])
AC_MSG_NOTICE([Build using API_DEFS=$API_DEFS])

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
AC_CONFIG_FILES([rc/Makefile])
AC_CONFIG_FILES([rc/trafficserver])
AC_CONFIG_FILES([iocore/aio/Makefile])
AC_CONFIG_FILES([iocore/cache/Makefile])
AC_CONFIG_FILES([iocore/cluster/Makefile])
AC_CONFIG_FILES([iocore/dns/Makefile])
AC_CONFIG_FILES([iocore/eventsystem/Makefile])
AC_CONFIG_FILES([iocore/hostdb/Makefile])
AC_CONFIG_FILES([iocore/Makefile])
AC_CONFIG_FILES([iocore/net/Makefile])
AC_CONFIG_FILES([iocore/utils/Makefile])
AC_CONFIG_FILES([libinktomi++/ink_config.h])
AC_CONFIG_FILES([libinktomi++/Makefile])
AC_CONFIG_FILES([librecords/Makefile])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([proxy/config/body_factory/default/Makefile])
AC_CONFIG_FILES([proxy/config/body_factory/Makefile])
AC_CONFIG_FILES([proxy/config/records.config])
AC_CONFIG_FILES([proxy/config/storage.config])
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
AC_CONFIG_FILES([proxy/mgmt2/html2/agent-installer/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/charting/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/configure/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/configure/helper/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/images/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/include/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/monitor/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/mrtg/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/html2/tune/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/preparse/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/stats/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/tools/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/utils/Makefile])
AC_CONFIG_FILES([proxy/mgmt2/web2/Makefile])
AC_CONFIG_FILES([proxy/stats/Makefile])
AC_CONFIG_FILES([example/Makefile])
# example plugins
AC_CONFIG_FILES([example/add-header/Makefile])
AC_CONFIG_FILES([example/append-transform/Makefile])
AC_CONFIG_FILES([example/basic-auth/Makefile])
AC_CONFIG_FILES([example/blacklist-0/Makefile])
AC_CONFIG_FILES([example/blacklist-1/Makefile])
AC_CONFIG_FILES([example/bnull-transform/Makefile])
AC_CONFIG_FILES([example/cache_plugin/Makefile])
AC_CONFIG_FILES([example/cache_scan/Makefile])
AC_CONFIG_FILES([example/file-1/Makefile])
AC_CONFIG_FILES([example/file_system_cache/Makefile])
AC_CONFIG_FILES([example/gzip-transform/Makefile])
AC_CONFIG_FILES([example/hello/Makefile])
AC_CONFIG_FILES([example/null-transform/Makefile])
AC_CONFIG_FILES([example/output-header/Makefile])
AC_CONFIG_FILES([example/prefetch/Makefile])
AC_CONFIG_FILES([example/protocol/Makefile])
AC_CONFIG_FILES([example/redirect-1/Makefile])
AC_CONFIG_FILES([example/query_remap/Makefile])
AC_CONFIG_FILES([example/remap/Makefile])
AC_CONFIG_FILES([example/replace-header/Makefile])
AC_CONFIG_FILES([example/response-header-1/Makefile])
AC_CONFIG_FILES([example/server-transform/Makefile])
AC_CONFIG_FILES([example/session-1/Makefile])
AC_CONFIG_FILES([example/thread-1/Makefile])
AC_CONFIG_FILES([example/thread-pool/Makefile])
# example app w/ standalone iocore
AC_CONFIG_FILES([example/app-template/Makefile])
AC_CONFIG_FILES([example/app-template/records.config])

# -----------------------------------------------------------------------------
# 7. autoheader TEMPLATES

AC_OUTPUT
