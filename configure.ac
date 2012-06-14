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
m4_define([TS_VERSION_S],[3.2.0])
m4_define([TS_VERSION_N],[3002000])

AC_INIT([Apache Traffic Server], TS_VERSION_S(), [dev@trafficserver.apache.org], [trafficserver],[http://trafficserver.apache.org])
AC_PREREQ([2.59])
AC_CONFIG_AUX_DIR([build/aux])
AC_CONFIG_SRCDIR([proxy/Main.cc])
AC_CONFIG_MACRO_DIR([build])
AM_INIT_AUTOMAKE([-Wall -Werror foreign no-installinfo no-installman 1.9.2])
AM_CONFIG_HEADER([lib/ts/ink_autoconf.h])

# Configure with --disable-silent-rules to get verbose output. For more info, see
# http://www.gnu.org/software/automake/manual/html_node/Automake-silent_002drules-Option.html
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Not sure this is needed, and seems to break with some versions of automake
#LT_INIT([dlopen])

# Libtool versioning uses different conventions on different
# platforms.  At least on FreeBSD, libtool uses an overly complex
# convention that attempts to solve problems that most people just
# don't have and which just causes confusion for most end users.
#
TS_VERSION_MAJOR=$((TS_VERSION_N() / 1000000 ))
TS_VERSION_MINOR=$(((TS_VERSION_N() / 1000) % 1000 ))
TS_VERSION_MICRO=$((TS_VERSION_N() % 1000 ))
TS_LIBTOOL_MAJOR=`echo $((${TS_VERSION_MAJOR} + ${TS_VERSION_MINOR}))`
TS_LIBTOOL_VERSION=$TS_LIBTOOL_MAJOR:$TS_VERSION_MICRO:$TS_VERSION_MINOR
TS_VERSION_STRING=TS_VERSION_S()
TS_VERSION_NUMBER=TS_VERSION_N()

#
# Substitute the above version numbers into the various files below.
#
AC_SUBST(TS_LIBTOOL_VERSION)
AC_SUBST(TS_VERSION_STRING)
AC_SUBST(TS_VERSION_NUMBER)
AC_SUBST(TS_VERSION_MAJOR)
AC_SUBST(TS_VERSION_MINOR)
AC_SUBST(TS_VERSION_MICRO)

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
TS_CONFIG_NICE([config.nice])

# XXX we can't just use AC_PREFIX_DEFAULT because that isn't subbed in
# by configure until it is too late.  Is that how it should be or not?
# Something seems broken here.
AC_PREFIX_DEFAULT([/usr/local/trafficserver])

# Get the layout here, so we can pass the required variables to Trafficserver
TS_ENABLE_LAYOUT(TrafficServer, [cachedir docdir])

# Reparse the configure arguments so we can override the layout.
TS_PARSE_ARGUMENTS

#
# Host detection
#
AC_CANONICAL_HOST
HOST_GUESS="$host"
AC_SUBST(HOST_GUESS)

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
  [AS_HELP_STRING([--with-user],[specify the system user [default=nobody]])],
  [
  with_user="$withval"
  ],[
  with_user="nobody"
  ]
)

default_group="`id -ng $with_user`"
AC_ARG_WITH([group],
  [AS_HELP_STRING([--with-group],[specify the system group [default=nobody]])],
  [
  with_group="$withval"
  ],[
  with_group=${default_group:-nobody}
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
# Fast SDK APIs, this disables the parameter checks (assert)
# on all APIs.
#

AC_MSG_CHECKING([whether to enable fast SDK APIs])
AC_ARG_ENABLE([fast-sdk],
  [AS_HELP_STRING([--enable-fast-sdk],[enable fast SDK APIs (no input parameter sanity checks)])],
  [],
  [enable_fast_sdk=no]
)
AC_MSG_RESULT([$enable_fast_sdk])
TS_ARG_ENABLE_VAR([use], [fast-sdk])
AC_SUBST(use_fast_sdk)

#
# Diags
#

AC_MSG_CHECKING([whether to enable diags])
AC_ARG_ENABLE([diags],
  [AS_HELP_STRING([--disable-diags],[turn off diags])],
  [],
  [enable_diags=yes]
)
AC_MSG_RESULT([$enable_diags])
TS_ARG_ENABLE_VAR([use], [diags])
AC_SUBST(use_diags)

#
# Build regression tests?
#

AC_MSG_CHECKING([whether to enable regression tests])
AC_ARG_ENABLE([tests],
  [AS_HELP_STRING([--disable-tests],[turn off regression tests])],
  [],
  [enable_tests=yes]
)
AC_MSG_RESULT([$enable_tests])
TS_ARG_ENABLE_VAR([has], [tests])
AC_SUBST(has_tests)
AM_CONDITIONAL([BUILD_TESTS], [test 0 -ne $has_tests])

#
# Micro
#
AC_MSG_CHECKING([whether to enable micro-TS])
AC_ARG_ENABLE([micro],
  [AS_HELP_STRING([--enable-micro],[enable micro-TS build [default=no]])],
  [],
  [enable_micro=no]
)
AC_MSG_RESULT([$enable_micro])
AS_IF([test "x$enable_micro" = "xyes"], [is_micro_build=1], [is_micro_build=0])
AC_SUBST(is_micro_build)

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
TS_ARG_ENABLE_VAR([has],[standalone-iocore])
AC_SUBST(has_standalone_iocore)
AS_IF([test 0 -ne $has_standalone_iocore],
  [AC_SUBST([IOCORE_MODULARIZED_DEFS],[""])
   AC_SUBST([ink_with_modules_def],["-DREC_BUILD_STAND_ALONE"])
   AC_SUBST([ink_with_modules_local],[""])
   AC_SUBST([ink_with_modules_process],[""])
  ],
  [AC_SUBST([ink_with_modules_def],["-DREC_BUILD_MGMT"])
   AC_SUBST([ink_with_modules_local],["-DLOCAL_MANAGER"])
   AC_SUBST([ink_with_modules_process],["-DPROCESS_MANAGER"])
  ]
)
AM_CONDITIONAL([STANDALONE_IOCORE], [test 0 -ne $has_standalone_iocore])

#
# Force some static linkage (for testing / development only)
#
AC_MSG_CHECKING([whether to build some static libts (dev only)])
AC_ARG_ENABLE([static-libts],
  [AS_HELP_STRING([--enable-static-libts],[build some static libts (dev only)])],
  [],
  [enable_static_libts=no]
)
AC_MSG_RESULT([$enable_static_libts])
TS_ARG_ENABLE_VAR([has],[static-libts])
AM_CONDITIONAL([STATIC_LIBTS], [test 0 -ne $has_static_libts])

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
AS_IF([test "x$enable_api" = "xyes"], [has_inkapi=1], [has_inkapi=0])
AC_SUBST(has_inkapi)

#
# WCCP
#
AC_MSG_CHECKING([whether to enable WCCP v2 support])
AC_ARG_ENABLE([wccp],
  [AS_HELP_STRING([--enable-wccp],[enable WCCP v2])],
  [],
  [enable_wccp=no]
)
AC_MSG_RESULT([$enable_wccp])
TS_ARG_ENABLE_VAR([has],[wccp])
AC_SUBST(has_wccp)
AM_CONDITIONAL([BUILD_WCCP], [test 0 -ne $has_wccp])

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
# use POSIX capabilities instead of user ID switching.
#
AC_MSG_CHECKING([whether to use POSIX capabilities])
AC_ARG_ENABLE([posix-cap],
  [AS_HELP_STRING([--disable-posix-cap],[Use user id switching instead of POSIX capabilities])],
  [],
  [enable_posix_cap="auto"]
)
AC_MSG_RESULT([$enable_posix_cap])

#
# use hwloc library when possible (can be disabled)
#
AC_MSG_CHECKING([whether to use hwloc library])
AC_ARG_ENABLE([hwloc],
  [AS_HELP_STRING([--disable-hwloc],[Don't use the hwloc library])],
  [],
  [enable_hwloc="yes"]
)
AC_MSG_RESULT([$enable_hwloc])

#
# Enble ccache explicitly (it's disabled by default, because of build problems in some cases)
#
AC_MSG_CHECKING([whether to enable ccache])
AC_ARG_ENABLE([ccache],
  [AS_HELP_STRING([--enable-ccache],[Enable ccache (can cause bad builds)])],
  [],
  [enable_ccache="no"]
)
AC_MSG_RESULT([$enable_ccache])

#
# Use TPROXY for connection transparency.
#
AC_MSG_CHECKING([whether to enable TPROXY based transparency])
AC_ARG_ENABLE([tproxy],
  [AS_HELP_STRING([--enable-tproxy[[=ARG]]],
                  [Use TPROXY to enable connection transparency.
                   'auto' or omitted for local system default,
		   'no' to disable,
		   'force' to use built in default,
		   number to use as IP_TRANSPARENT sockopt.
		   [default=auto]
		  ])
  ],
  [],
  [enable_tproxy="auto"]
)
AC_MSG_RESULT([$enable_tproxy])

#
# Disable our freelist implementation, reverting it to whatever
# allocator (malloc, tcmalloc or jemalloc) that is in use. This is
# useful for debugging.
#
AC_MSG_CHECKING([whether to disable freelist])
AC_ARG_ENABLE([freelist],
  [AS_HELP_STRING([--disable-freelist],[turn off freelist and use allocators])],
  [],
  [enable_freelist="yes"]
)
AC_MSG_RESULT([$enable_freelist])
TS_ARG_ENABLE_VAR([use], [freelist])
AC_SUBST(use_freelist)

#
# Configure how many stats to allocate for plugins. Default is 512.
#
AC_ARG_WITH([max-api-stats],
  [AS_HELP_STRING([--with-max-api-stats],[max number of plugin stats [default=512]])],
  [max_api_stats=$withval],
  [max_api_stats=512]
)
AC_SUBST(max_api_stats)

#
# Max host name length that we deal with in URLs.
#
AC_ARG_WITH([max-host-name-len],
  [AS_HELP_STRING([--with-max-host-name-len],[max host name length [default=256]])],
  [max_host_name_len=$withval],
  [max_host_name_len=256]
)
AC_SUBST(max_host_name_len)

#
# EventProcessor thread configurations
#

AC_ARG_WITH([max-event-threads],
  [AS_HELP_STRING([--with-max-event-threads],[max number of event threads [default=4096]])],
  [max_event_threads=$withval],
  [max_event_threads=4096]
)
AC_SUBST(max_event_threads)

AC_ARG_WITH([max-threads-per-type],
  [AS_HELP_STRING([--with-max-threads-per-type],[max number of threads per event type [default=3072]])],
  [max_threads_per_type=$withval],
  [max_threads_per_type=3072]
)
AC_SUBST(max_threads_per_type)


#
# Installation directories
# For each var the following is evaluated
# foo      Standard variable  eg. ${prefix}/foo
# rel_foo  Relative to prefix eg. foo
#
TS_SUBST_LAYOUT_PATH([prefix])
TS_SUBST_LAYOUT_PATH([exec_prefix])
TS_SUBST_LAYOUT_PATH([bindir])
TS_SUBST_LAYOUT_PATH([sbindir])
TS_SUBST_LAYOUT_PATH([libdir])
TS_SUBST_LAYOUT_PATH([libexecdir])
TS_SUBST_LAYOUT_PATH([infodir])
TS_SUBST_LAYOUT_PATH([mandir])
TS_SUBST_LAYOUT_PATH([sysconfdir])
TS_SUBST_LAYOUT_PATH([datadir])
TS_SUBST_LAYOUT_PATH([installbuilddir])
TS_SUBST_LAYOUT_PATH([includedir])
TS_SUBST_LAYOUT_PATH([localstatedir])
TS_SUBST_LAYOUT_PATH([runtimedir])
TS_SUBST_LAYOUT_PATH([logdir])
TS_SUBST_LAYOUT_PATH([cachedir])
TS_SUBST_LAYOUT_PATH([docdir])

TS_SUBST([pkgbindir])
TS_SUBST([pkgsbindir])
TS_SUBST([pkglibdir])
TS_SUBST([pkglibexecdir])
TS_SUBST([pkgsysconfdir])
TS_SUBST([pkgdatadir])
TS_SUBST([pkglocalstatedir])
TS_SUBST([pkgruntimedir])
TS_SUBST([pkglogdir])
TS_SUBST([pkgcachedir])
TS_SUBST([pkgdocdir])


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

AC_PROG_CC
AC_PROG_CXX

# Check for ccache (if explicitly enabled)
if test "x$enable_ccache" = "xyes"; then
    AC_CHECK_PROG([CCACHE],[ccache],[ccache],[])
    if test "x${CCACHE}" = "xccache"; then
	CC="$CCACHE $CC"
	CXX="$CCACHE $CXX"
    fi
fi

dnl AC_PROG_SED is only avaliable in recent autoconf versions.
dnl Use AC_CHECK_PROG instead if AC_PROG_SED is not present.
ifdef([AC_PROG_SED],
      [AC_PROG_SED],
      [AC_CHECK_PROG(SED, sed, sed)])

dnl Same with AM_PROG_AR, but it doesn't seem to be needed in older versions.
ifdef([AM_PROG_AR],
      [AM_PROG_AR])


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
AS_IF([test "x$enable_wccp" = "xyes"],
  [

    # If flex is found, set output variable LEX to ‘flex’ 
    # Otherwise set LEX to ‘lex’ 
    # If neither variant is available, set LEX to ‘:’;
    AC_PROG_LEX
    AS_IF([test "x$LEX" = "x:"  ],
       AC_MSG_ERROR([Need flex when enabling WCCP])
    )

    # If bison is found, set output variable YACC to ‘bison -y’. 
    # Otherwise, if byacc is found, set YACC to ‘byacc’. 
    # Otherwise set YACC to ‘yacc’. 
    AC_PROG_YACC
    AS_IF([test "x$YACC" = "xyacc" ],
	AC_CHECK_PROG([YACCBIN],[yacc],[yacc],[])
	AS_IF([test "x$YACCBIN" = "x"],
		AC_MSG_ERROR([Need at YACC when enabling WCCP])
	)
    )

  ]
)

AC_PATH_PROG([DOXYGEN], [doxygen]) # needed for Doxygen
AC_PATH_PROG([PERL],[perl],[not found])
AS_IF([test "x$PERL" = "xnot found"],
  [AC_MSG_ERROR([check for perl failed. Have you installed perl?])]
)
AC_ARG_VAR([DOXYGEN], [full path of Doxygen executable])
AC_ARG_VAR([PERL], [full path of Perl executable])

CFLAGS="${REAL_CFLAGS}"
CXXFLAGS="${REAL_CXXFLAGS}"

# Check for GNU-style -On and for Solaris Studio style -xOn.
AC_MSG_CHECKING([checking whether to auto-set compiler optimization flags])
has_optimizer_flags=`echo "$CFLAGS $CXXFLAGS" | ${AWK} '$0 !~ /-x?O.?/{print "no"}'`
AS_IF([test "x${has_optimizer_flags}" = "xno"],
	[
		optimizing_flags='-O3'
		AC_MSG_RESULT([yes ${optimizing_flags}])
	],
	[
		has_optimizer_flags='yes'
		optimizing_flags=''
		AC_MSG_RESULT([no])
	]
)

base_cc=`basename $CC`
# These are shortcuts used in combination for the compiler options below
case $host_os in
  linux*)
    case $base_cc in
    icc)
      # -Wall goes crazy, so turned these specific checks off for now:
      #
      #  111 is "statement is unrecahable"
      #  279 is "controlling expression is constant", triggered by our asserts
      #  383 is "value copied to temporary, reference to temporary used"
      #  444 is "destructor for base class is not virtual"
      #  522 is "function "xyz" redeclared "inline" after being called
      #  873 is "has no corresponding operator delete". ToDo: we should fix.
      #  981 is "operands are evaluated in unspecified order"
      # 1418 is "external function definition with no prior declaration"
      # 1419 is "external declaration in primary source file"
      # 1572 is "floating-point equality and inequality comparisons are unreliable"
      # 1720 is "operator new" has no corresponding member operator delete"
      # 2256 is "non-pointer conversion from "int" to "unsigned char" "
      # 2259 is "non-pointer conversion from "int" to "unsigned char" "
      # 
      # TODO: We should try to eliminate more of these -wd exclusions.
      common_opt="-pipe -Wall -wd111 -wd279 -wd383 -wd522 -wd444 -wd873 -wd981 -wd1418 -wd1419 -wd1572 -wd1720 -wd2256 -wd2259"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimization_flags -axsse4.2 -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
      ;;
    clang)
      debug_opt="-ggdb3 $common_opt -Werror -Qunused-arguments"
      release_opt="-g $common_opt $optimizing_flags -fno-strict-aliasing -Werror -Qunused-arguments"
      cxx_opt="-Wno-invalid-offsetof -Werror -Qunused-arguments"
      ;;
    *) # gcc
      # This is useful for finding odd conversions
      #    common_opt="-pipe -Wall -Werror -Wconversion -Wno-sign-conversion"
      common_opt="-pipe -Wall -Werror"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
      ;;
    esac
    ;; # linux*)
  darwin*)
    case $base_cc in
    clang)
      common_opt="-pipe -Wall -Werror -Wno-deprecated-declarations"
      debug_opt="$common_opt"
      release_opt="-g $common_opt $optimizing_flags -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
      ;;
    *) # gcc
      common_opt="-pipe -Wall -Werror -Wno-deprecated-declarations"
      debug_opt="$common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
      ;;
    esac
    # ToDo: This seems semi-kludgy, but useful for MacPort's I think.
    TS_ADDTO(CPPFLAGS, [-I/opt/local/include])
    TS_ADDTO(LDFLAGS, [-L/opt/local/lib])
    ;; # darwin*)
  freebsd*|kfreebsd*)
    case $base_cc in
    clang)
      common_opt="-pipe -Wall -Werror -Qunused-arguments"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof -Qunused-arguments"
      ;;
    *) # gcc
      common_opt="-pipe -Wall -Werror"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
      ;;
    esac
    TS_ADDTO(LDFLAGS, [-L/usr/local/lib])
    ;;
  solaris*)
    if test "x${base_cc}" = "xcc"; then
      common_opt="-mt -m64 -D__WORDSIZE=64" # FIXME: arch should be detected
      debug_opt="-g $common_opt"
      release_opt="-g $common_opt $optimizing_flags"
      cxx_opt="-library=stlport4"
      cxx_dbg="+w2"
      cxx_rel="-erroff"
      AC_DEFINE([_POSIX_PTHREAD_SEMANTICS],1,
       [posix thread semantics]
      )
      if test "$solaris_use_stlport4" != yes; then
        postdeps_CXX="-library=Crun"
      fi
    else # gcc
      common_opt="-pipe -Wall -Werror"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
    fi
    TS_ADDTO(LDFLAGS, [-L/lib])
    TS_ADDTO(LDFLAGS, [-L/usr/local/lib])
    ;;
  *)
    common_opt="-pipe -Wall -Werror"
    debug_opt="-ggdb3 $common_opt"
    release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
    cxx_opt="-Wno-invalid-offsetof"
    ;;
esac

cc_oflag_opt=$release_opt
cc_oflag_dbg=$debug_opt
cxx_oflag_opt="$release_opt $cxx_opt $cxx_rel"
cxx_oflag_dbg="$debug_opt $cxx_opt $cxx_dbg"

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


# Checks for TCP defer accept
case $host_os in
  linux*)
    defer_accept=45
    ;;
  *)
    defer_accept=1
    ;;
esac
AC_SUBST(defer_accept)


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
  TS_ADDTO(CFLAGS, [$cpu_architecture])
  TS_ADDTO(CXXFLAGS, [$cpu_architecture])
fi

# 64-bit LFS support
#
TS_ADDTO(CPPFLAGS, [-D_LARGEFILE64_SOURCE=1])
if test "${ac_cv_sizeof_voidp}" = "8"; then
  TS_ADDTO(CPPFLAGS, [-D_COMPILE64BIT_SOURCE=1])
else
  TS_ADDTO(CPPFLAGS, [-D_FILE_OFFSET_BITS=64])
fi
TS_ADDTO(CPPFLAGS, [-D_GNU_SOURCE])
TS_ADDTO(CPPFLAGS, [-D_REENTRANT])

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
  kfreebsd*)
    EXTRA_CXX_LDFLAGS="-rdynamic"
    host_os_def=freebsd
    TS_ADDTO(CPPFLAGS, [-Dkfreebsd])
    ;;
  solaris*)
    host_os_def=solaris
    ;;
  openbsd*)
    host_os_def=openbsd
    ;;
  *)
    EXTRA_CXX_LDFLAGS="-rdynamic"
    host_os_def=unknown
   ;;
esac
TS_ADDTO(CPPFLAGS, [-D$host_os_def])

AC_MSG_NOTICE([Build for host OS: $host_os, arch: $host_cpu, optimization: $host_os_def])

#
# _Here_ is where we go ahead and add the _optimizations_ to already
#  existing CFLAGS/CXXFLAGS if some special values had been set.
#
if test "x${enable_debug}" = "xyes"; then
  TS_ADDTO(CFLAGS, [${cc_oflag_dbg}])
  TS_ADDTO(CXXFLAGS, [${cxx_oflag_dbg}])
  TS_ADDTO(CPPFLAGS, [-DDEBUG -D_DEBUG])
else
  TS_ADDTO(CFLAGS, [${cc_oflag_opt}])
  TS_ADDTO(CXXFLAGS, [${cxx_oflag_opt}])
fi

#
# Note:  These are site-specific macro's that do various tests
#         on the selected compilers.  There was some tunning
#         associated with our not wanting to use GNU for _everything_.
# Note:  This macro may set certain parameters when run.
#


# -----------------------------------------------------------------------------
# 4. CHECK FOR LIBRARIES

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

#
# Check for SSL presence and usability
TS_CHECK_CRYPTO
if test "x${enable_crypto}" != "xyes"; then
  AC_MSG_ERROR([Need at least one SSL library, --with-openssl is supported])
fi

#
# Check for NextProtocolNegotiation TLS extension support.
TS_CHECK_CRYPTO_NEXTPROTONEG

#
# Check for ServerNameIndication TLS extension support.
TS_CHECK_CRYPTO_SNI

#
# Check for zlib presence and usability
TS_CHECK_ZLIB

#
# Check for lzma presence and usability
TS_CHECK_LZMA

#
# Tcl macros provided by build/tcl.m4
#
# this will error out if tclConfig.sh is not found
SC_PATH_TCLCONFIG

# if tclConfig.sh loads properly, assume libraries are there and working
SC_LOAD_TCLCONFIG

# expect tclConfig.sh to populate TCL_LIB_FLAG and TCL_INCLUDE_SPEC
case $host_os in
  darwin*)
    TCL_LIB_SPEC="-ltcl" # OSX fails to populate this variable
    ;;
  *)
   ;;
esac
AC_SUBST([LIBTCL],[$TCL_LIB_SPEC])

case $host_os in
  freebsd*|kfreebsd*)
    TS_ADDTO(CPPFLAGS, [-I/usr/local/include])
    ;;
esac

if test "x${TCL_INCLUDE_SPEC}" != "x-I/usr/include"; then
   TS_ADDTO(CPPFLAGS, [$TCL_INCLUDE_SPEC])
fi

#
# Check for XML parser
#
TS_CHECK_XML
if test "x${enable_xml}" != "xyes"; then
  AC_MSG_ERROR([Need at least one XML library, --with-expat is supported])
fi

TS_FLAG_FUNCS([clock_gettime kqueue epoll_ctl posix_memalign posix_fadvise lrand48_r srand48_r port_create])
TS_FLAG_FUNCS([strlcpy strlcat])

AC_SUBST(has_clock_gettime)
AC_SUBST(has_posix_memalign)
AC_SUBST(has_posix_fadvise)
AC_SUBST(has_lrand48_r)
AC_SUBST(has_srand48_r)
AC_SUBST(has_strlcpy)
AC_SUBST(has_strlcat)

# Check for eventfd() and sys/eventfd.h (both must exist ...)
TS_FLAG_HEADERS([sys/eventfd.h], [has_eventfd=1], [has_eventfd=0], [])
if test "x${has_eventfd}" = "xyes"; then
  AS_IF([test "x$enable_eventfd" = "xyes"],
    [TS_FLAG_FUNCS([eventfd])]
  )
fi
AC_SUBST(has_eventfd)

#
# Check for pcre library
#
TS_CHECK_PCRE
if test "x${enable_pcre}" != "xyes"; then
  AC_MSG_ERROR([Cannot find pcre library. Configure --with-pcre=DIR])
fi

has_backtrace=0
# Check for backtrace() support
TS_FLAG_HEADERS([execinfo.h], [has_backtrace=1],[])
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

use_epoll=0
use_kqueue=0
use_port=0
if test "$ac_cv_func_epoll_ctl" = "yes"; then
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
#  ink_stack_trace.cc in lib/ts.  Because this
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

# Check for POSIX capabilities library.
# If we don't find it, disable checking for header.
use_posix_cap=0
AS_IF([test "x$enable_posix_cap" != "xno"],
  AC_CHECK_LIB([cap],[cap_set_proc],
    [AC_SUBST([LIBCAP], ["-lcap"])
     use_posix_cap=1
    ],[
     AS_IF([test "x$enable_posix_cap" == "xyes"], [
      AC_MSG_FAILURE([POSIX capabilities enabled but system library not found.])
     ],[
      [enable_posix_cap=no]
    ])
   ]
  )
)
AC_SUBST(use_posix_cap)

# Check for hwloc library.
# If we don't find it, disable checking for header.
use_hwloc=0
AS_IF([test "x$enable_hwloc" = "xyes"],
  AC_CHECK_LIB([hwloc],[hwloc_topology_init],
    [AC_SUBST([LIBHWLOC], ["-lhwloc"])
     use_hwloc=1
    ],
    [enable_hwloc=no]
  )
)
AC_SUBST(use_hwloc)

#
# Check for tcmalloc and jemalloc
TS_CHECK_JEMALLOC
TS_CHECK_TCMALLOC

#
# Determine ARG_MAX
# Default to 2048 if we cannot resolve ARG_MAX for some reason
AC_ARG_WITH([arg_max],
  [AS_HELP_STRING([--with-arg-max=[ARG_MAX]],[override the computed maxmium argument length])],
  [
  arg_max="$withval"
  ],[
  arg_max="`getconf ARG_MAX`"
  if test "x${arg_max}" = x ; then
    arg_max=2048
  fi
  ]
)
AC_SUBST(arg_max)

#
# Check for libreadline/libedit
AX_LIB_READLINE

# -----------------------------------------------------------------------------
# 5. CHECK FOR HEADER FILES

TS_FLAG_HEADERS([sys/epoll.h \
                  sys/event.h \
                  machine/endian.h \
                  endian.h \
                  sys/sysinfo.h \
                  sys/systeminfo.h \
                  netinet/in.h \
                  netinet/in_systm.h \
                  netinet/tcp.h \
                  sys/ioctl.h \
                  sys/byteorder.h \
                  sys/sockio.h \
		  sys/prctl.h \
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
                  sys/param.h \
                  sys/sysmacros.h \
                  math.h \
                  stdint.h \
                  net/ppp_defs.h \
                  ifaddrs.h\
		  readline/readline.h \
		  editline/readline.h ])

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
AC_SUBST(ifaddrsh)
AC_SUBST(readline_readlineh)

TS_FLAG_HEADERS([sys/sysctl.h], [], [],
                 [[#ifdef HAVE_SYS_PARAM_H
                    #include <sys/param.h>
                   #endif
                 ]])

TS_FLAG_HEADERS([sys/mount.h], [], [],
                 [[#ifdef HAVE_SYS_PARAM_H
                    #include <sys/param.h>
                   #endif
                 ]])

TS_FLAG_HEADERS([arpa/inet.h], [], [],
                 [[#ifdef HAVE_SYS_TYPES_H
                    #include <sys/types.h>
                   #endif
                   #ifdef HAVE_NETINET_IN_H
                    #include <netinet/in.h>
                   #endif
                 ]])

TS_FLAG_HEADERS([netinet/ip.h], [], [],
                 [[#ifdef HAVE_SYS_TYPES_H
                    #include <sys/types.h>
                   #endif
                   #ifdef HAVE_NETINET_IN_H
                    #include <netinet/in.h>
                   #endif
                   #ifdef HAVE_NETINET_IN_SYSTM_H
                    #include <netinet/in_systm.h>
                   #endif
                 ]])

TS_FLAG_HEADERS([netinet/ip_icmp.h], [], [],
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

# BSD-derived systems populate the socket length in the structure itself. It's
# redundant to check all of these, but hey, I need the typing practice.
AC_CHECK_MEMBER([struct sockaddr.sa_len], [], [], [#include <netinet/in.h>])
AC_CHECK_MEMBER([struct sockaddr_in.sin_len], [], [], [#include <netinet/in.h>])
AC_CHECK_MEMBER([struct sockaddr_in6.sin6_len], [], [], [#include <netinet/in.h>])

if test "x${ac_cv_member_sockaddr_sa_len}" = xyes; then
    AC_DEFINE(HAVE_STRUCT_SOCKADDR_SA_LEN, 1,
	    [Whether struct sockaddr_in has the sa_len member])
fi

if test "x${ac_cv_member_sockaddr_in_sin_len}" = xyes ; then
AC_DEFINE(HAVE_STRUCT_SOCKADDR_IN_SIN_LEN, 1,
	    [Whether struct sockaddr_in has the sin_len member])
fi

if test "x${ac_cv_member_sockaddr_in6_sin6_len}" = xyes; then
    AC_DEFINE(HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN, 1,
	    [Whether struct sockaddr_in6 has the sin6_len member])
fi

if test "x${with_profiler}" = "xyes"; then
TS_FLAG_HEADERS([google/profiler.h \
                  ], [], [])
fi

if test "x${enable_posix_cap}" != "xno"; then
  AC_CHECK_HEADERS([sys/capability.h],
    [],
    [AC_MSG_FAILURE([Found POSIX capabilities library but not the header sys/capability.h. POSIX capabilities are not a required feature, you can disable then with --disable-posix-cap])],
    []
  )
fi

if test "x${enable_hwloc}" = "xyes"; then
  AC_CHECK_HEADERS([hwloc.h],
    [],
    [AC_MSG_FAILURE([hwloc header not found. Try --disable-hwloc])],
    []
  )
fi

#
# Configure sockopt value for TPROXY. Look at the enable flag.
# Value 'no' means user forced disable, don't check anything else.
#       'auto' means user didn't say, so silently enable/disable
#              based on success.
#       A numeric value means enable, don't check, use that value.
#       Anything else means user forced, fail if value not found
#       in header file.
# We can't just include linux/in.h because it's incompatible with
# netinet/in.h.
# Verify the file exists (is readable), scan for the value we need,
# if found export the value and enable use of the value.
#
ip_transparent=0
use_tproxy=0
tproxy_header=/usr/include/linux/in.h
tproxy_usage_enable="
    --enable-tproxy Enable the feature and validate."
tproxy_usage_default="
    --enable-tproxy=force Enable using default sockopt value, no validation."
tproxy_usage_numeric="
    --enable-tproxy=X where X is numeric
                      Enable, use X for sockopt value, no validation."
tproxy_usage_disable="
    --disable-tproxy Disable feature, no validation."
proxy_usage="$tproxy_usage_enable$tproxy_usage_default$tproxy_usage_numeric$tproxy_usage_disable"

AC_MSG_CHECKING([whether to enable transparent proxy])
AS_IF([test "x$enable_tproxy" != "xno"], [
  AS_IF([test "$use_posix_cap" -eq 0], [
    AS_IF([test "x$enable_tproxy" = xauto], [
      AC_MSG_RESULT([no])
    ],[
      AC_MSG_FAILURE([TPROXY feature requires POSIX capabilities.])
    ])
  ],[
    AC_MSG_CHECKING([for TPROXY sockopt IP_TRANSPARENT])
    case "$enable_tproxy" in
      [[0-9][0-9]*])
	ip_transparent=$enable_tproxy
	use_tproxy=1
	AC_MSG_RESULT([forced to $ip_transparent])
	;;
      force)
	ip_transparent=19
	use_tproxy=1
	AC_MSG_RESULT([forced to $ip_transparent])
	;;
      yes|auto)
        AS_IF([test -r $tproxy_header], [
	  ip_transparent=`$AWK "/^#define[ \t]+IP_TRANSPARENT[ \t]+[0-9]+/{print \\$3}" $tproxy_header`
	  AS_IF([test "x$ip_transparent" != "x"], [
	    use_tproxy=1
	    AC_MSG_RESULT([set to $ip_transparent])
	  ],[
	    ip_transparent=0
	    AS_IF([test "x$enable_tproxy" = xauto], [
	     AC_MSG_RESULT([no])
	    ],[
	      AC_MSG_RESULT([failed])
	      AC_MSG_FAILURE([tproxy feature enabled but the sockopt value was not found in $tproxy_header. Try one of$tproxy_usage_default$tproxy_usage_numeric$tproxy_usage_disable])
	    ])
	  ])
	],[
	  AS_IF([test "x$enable_tproxy" = xauto], [
	    AC_MSG_RESULT([no])
	  ],[
	    AC_MSG_RESULT([failed])
	    AC_MSG_FAILURE([tproxy feature enabled but the header file $tproxy_header was not readable. Try one of$tproxy_usage_default$tproxy_usage_numeric$tproxy_usage_disable])
	  ])
	])
	;;
      *)
	AC_MSG_RESULT([failed])
	AC_MSG_FAILURE([Invalid argument to feature tproxy.$tproxy_usage])
	;;
      esac
  ])
])

AC_SUBST(use_tproxy)
AC_SUBST(ip_transparent)

AC_MSG_CHECKING([for SO_MARK])
AC_TRY_COMPILE([
#include <sys/socket.h>
], [
if( SO_MARK > 0) return 0;
else return 1;
], [has_so_mark=1]
msg=yes, [
has_so_mark=0
msg=no ] )

AC_MSG_RESULT([$msg])
AC_SUBST(has_so_mark)

AC_MSG_CHECKING([for IP_TOS])
AC_TRY_COMPILE([
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
],[
if( IP_TOS > 0) return 0;
else return 1;
], [has_ip_tos=1]
msg=yes, [
has_ip_tos=0
msg=no ] )

AC_MSG_RESULT([$msg])
AC_SUBST(has_ip_tos)

TS_CHECK_LOOPBACK_IFACE
TS_CHECK_GETHOSTBYNAME_R_STYLE
TS_CHECK_MACRO_IN6_IS_ADDR_UNSPECIFIED

#
# use modular IOCORE
#
iocore_include_dirs="-I\$(top_srcdir)/iocore/eventsystem \
-I\$(top_srcdir)/iocore/net \
-I\$(top_srcdir)/iocore/aio \
-I\$(top_srcdir)/iocore/hostdb \
-I\$(top_srcdir)/iocore/cache \
-I\$(top_srcdir)/iocore/cluster \
-I\$(top_srcdir)/iocore/utils \
-I\$(top_srcdir)/iocore/dns"

# Testing Framework suffix generation hack
TFW_PACKAGE_SUFFIX=$os_type

MGMT_DEFS="$MGMT_DEFS -DMGMT_USE_SYSLOG"

AC_MSG_NOTICE([Build using CC=$CC])
AC_MSG_NOTICE([Build using CXX=$CXX])
AC_MSG_NOTICE([Build using CPP=$CPP])
AC_MSG_NOTICE([Build using CFLAGS=$CFLAGS])
AC_MSG_NOTICE([Build using SHARED_CFLAGS=$SHARED_CFLAGS])
AC_MSG_NOTICE([Build using CXXFLAGS=$CXXFLAGS])
AC_MSG_NOTICE([Build using SHARED_CXXFLAGS=$SHARED_CXXFLAGS])
AC_MSG_NOTICE([Build using SHARED_CXXLINKFLAGS=$SHARED_LINKCXXFLAGS])
AC_MSG_NOTICE([Build using CPPFLAGS=$CPPFLAGS])
AC_MSG_NOTICE([Build using LDFLAGS=$LDFLAGS])
AC_MSG_NOTICE([Build using SHARED_LDFLAGS=$SHARED_LDFLAGS])
AC_MSG_NOTICE([Build using EXTRA_CC_LDFLAGS=$EXTRA_CC_LDFLAGS])
AC_MSG_NOTICE([Build using EXTRA_CXX_LDFLAGS=$EXTRA_CXX_LDFLAGS])
AC_MSG_NOTICE([Build using LIBTOOL_LINK_FLAGS=$LIBTOOL_LINK_FLAGS])
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
AC_SUBST([LIBTOOL_LINK_FLAGS])
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

AC_CONFIG_FILES([lib/ts/ink_config.h proxy/api/ts/ts.h])
AC_CONFIG_FILES([tools/tsxs])
AC_CONFIG_FILES([doc/Doxyfile])
AC_CONFIG_FILES([doc/Makefile])
AC_CONFIG_FILES([rc/Makefile])
AC_CONFIG_FILES([rc/trafficserver])
AC_CONFIG_FILES([rc/trafficserver.xml])
AC_CONFIG_FILES([rc/trafficserver.conf])
AC_CONFIG_FILES([rc/trafficserver.service])
AC_CONFIG_FILES([iocore/aio/Makefile])
AC_CONFIG_FILES([iocore/cache/Makefile])
AC_CONFIG_FILES([iocore/cluster/Makefile])
AC_CONFIG_FILES([iocore/dns/Makefile])
AC_CONFIG_FILES([iocore/eventsystem/Makefile])
AC_CONFIG_FILES([iocore/hostdb/Makefile])
AC_CONFIG_FILES([iocore/Makefile])
AC_CONFIG_FILES([iocore/net/Makefile])
AC_CONFIG_FILES([iocore/utils/Makefile])
AC_CONFIG_FILES([lib/Makefile])
AC_CONFIG_FILES([lib/ts/Makefile])
AC_CONFIG_FILES([lib/records/Makefile])
AC_CONFIG_FILES([lib/tsconfig/Makefile])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([proxy/config/body_factory/default/Makefile])
AC_CONFIG_FILES([proxy/config/body_factory/Makefile])
AC_CONFIG_FILES([proxy/config/records.config.default proxy/config/storage.config.default])
AC_CONFIG_FILES([proxy/config/Makefile])
AC_CONFIG_FILES([proxy/congest/Makefile])
AC_CONFIG_FILES([proxy/hdrs/Makefile])
AC_CONFIG_FILES([proxy/http/Makefile])
AC_CONFIG_FILES([proxy/http/remap/Makefile])
AC_CONFIG_FILES([proxy/logging/Makefile])
AC_CONFIG_FILES([proxy/Makefile])
AC_CONFIG_FILES([mgmt/api/Makefile])
AC_CONFIG_FILES([mgmt/api/remote/Makefile])
AC_CONFIG_FILES([mgmt/api/include/Makefile])
AC_CONFIG_FILES([mgmt/cli/Makefile])
AC_CONFIG_FILES([mgmt/cluster/Makefile])
AC_CONFIG_FILES([mgmt/Makefile])
AC_CONFIG_FILES([mgmt/preparse/Makefile])
AC_CONFIG_FILES([mgmt/stats/Makefile])
AC_CONFIG_FILES([mgmt/tools/Makefile])
AC_CONFIG_FILES([mgmt/utils/Makefile])
AC_CONFIG_FILES([mgmt/web2/Makefile])
AC_CONFIG_FILES([proxy/stats/Makefile])
AC_CONFIG_FILES([lib/wccp/Makefile])
AC_CONFIG_FILES([proxy/api/ts/Makefile])
# Traffic Cop
AC_CONFIG_FILES([cop/Makefile])
# production plugins
AC_CONFIG_FILES([plugins/Makefile])
AC_CONFIG_FILES([plugins/conf_remap/Makefile])
AC_CONFIG_FILES([plugins/regex_remap/Makefile])
AC_CONFIG_FILES([plugins/header_filter/Makefile])
AC_CONFIG_FILES([plugins/stats_over_http/Makefile])
# various tools
AC_CONFIG_FILES([tools/Makefile])
# example plugins
AC_CONFIG_FILES([example/Makefile])
AC_CONFIG_FILES([example/add-header/Makefile])
AC_CONFIG_FILES([example/append-transform/Makefile])
AC_CONFIG_FILES([example/basic-auth/Makefile])
AC_CONFIG_FILES([example/blacklist-0/Makefile])
AC_CONFIG_FILES([example/blacklist-1/Makefile])
AC_CONFIG_FILES([example/bnull-transform/Makefile])
AC_CONFIG_FILES([example/cache_scan/Makefile])
AC_CONFIG_FILES([example/file-1/Makefile])
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
AC_CONFIG_FILES([example/app-template/Makefile example/app-template/records.config])

# -----------------------------------------------------------------------------
# 7. autoheader TEMPLATES

AC_OUTPUT
