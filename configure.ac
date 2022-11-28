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
m4_define([TS_VERSION_S],[10.0.0])
m4_define([TS_VERSION_N],[10000000])

AC_INIT([Apache Traffic Server],[TS_VERSION_S()],[dev@trafficserver.apache.org],[trafficserver],[https://trafficserver.apache.org])
AC_PREREQ([2.69])
AC_CONFIG_AUX_DIR([build/_aux])
AC_CONFIG_SRCDIR([src/traffic_server/traffic_server.cc])
AC_CONFIG_MACRO_DIR([build])

# NOTE: we turn off portability warnings because the clang-tidy targets use
# GNU make extensions to filter the sources list.
AM_INIT_AUTOMAKE([-Wall -Werror -Wno-portability tar-ustar foreign no-installinfo no-installman subdir-objects 1.9.2])

# See discussion at https://autotools.io/automake/maintainer.html.
AM_MAINTAINER_MODE([enable])

# Enable a recursive "tidy" rule for clang-tidy.
m4_ifdef([AM_EXTRA_RECURSIVE_TARGETS], [AM_EXTRA_RECURSIVE_TARGETS([clang-tidy])])

AC_CONFIG_HEADERS([include/ink_autoconf.h])

# Configure with --disable-silent-rules to get verbose output. For more info, see
# http://www.gnu.org/software/automake/manual/html_node/Automake-silent_002drules-Option.html
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

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
#pragma once
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

AC_ARG_WITH([build-number],
  [AS_HELP_STRING([--with-build-number],[specify a version string for this build])],
  [ build_number="$withval" ]
)

#
# Build environment
#
build_person="`id -nu | sed -e 's/\\\\/\\\\\\\\/g'`"
build_group="`id -ng | sed -e 's/\\\\/\\\\\\\\/g'`"
build_machine="`uname -n | sed -e 's/\\\\/\\\\\\\\/g'`"
AC_SUBST([build_machine])
AC_SUBST([build_person])
AC_SUBST([build_group])
AC_SUBST([build_number])

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

AC_MSG_CHECKING([whether to enable mime sanity check])
AC_ARG_ENABLE([mime-sanity-check],
  [AS_HELP_STRING([--enable-mime-sanity-check],[turn on mime sanity check])],
  [],
  [enable_mime_sanity_check=no]
)
AC_MSG_RESULT([$enable_mime_sanity_check])

AC_MSG_CHECKING([whether to enable all asserts, a cheaper debug])
AC_ARG_ENABLE([all-asserts],
  [AS_HELP_STRING([--enable-all-asserts],[turn on all code asserts, both debug and release])],
  [],
  [enable_all_asserts=no]
)
AC_MSG_RESULT([$enable_all_asserts])

AC_MSG_CHECKING([whether to enable event tracker])
AC_ARG_ENABLE([event-tracker],
  [AS_HELP_STRING([--enable-event-tracker],[turn on event tracker])],
  [],
  [enable_event_tracker=no]
)
AC_MSG_RESULT([$enable_event_tracker])

# Enable code coverage instrumentation only if requested by the user.
AC_MSG_CHECKING([whether to code coverage])
AC_ARG_ENABLE([coverage],
  [AS_HELP_STRING([--enable-coverage],[generate code coverage instrumentation])],
  [],
  [enable_coverage=no]
)
AC_MSG_RESULT([$enable_coverage])

#
# Enable -Werror. We want this enabled by default for developers, but disabled by default
# for end users (because we don't want released versions to suffer from compiler warning hell).
#
AC_MSG_CHECKING([whether to enable -Werror])
AC_ARG_ENABLE([werror],
  [AS_HELP_STRING([--enable-werror],[turn compiler warnings into errors])],
  [],
  [enable_werror=no]
)
AC_MSG_RESULT([$enable_werror])

# Enable ASAN for the builds
AC_MSG_CHECKING([whether to enable asan])
AC_ARG_ENABLE([asan],
  [AS_HELP_STRING([--enable-asan],[enable Address Sanitizer])],
  [],
  [enable_asan=no]
)
AC_MSG_RESULT([$enable_asan])

# Enable LSAN in stand-alone mode for the builds
AC_MSG_CHECKING([whether to enable lsan])
AC_ARG_ENABLE([lsan],
  [AS_HELP_STRING([--enable-lsan],[enable stand-alone Leak Sanitizer])],
  [],
  [enable_lsan=no]
)
AC_MSG_RESULT([$enable_lsan])

# Enable TSAN for the builds
AC_MSG_CHECKING([whether to enable tsan])
AC_ARG_ENABLE([tsan],
  [AS_HELP_STRING([--enable-tsan],[turn on Thread Sanitizer])],
  [],
  [enable_tsan=no]
)
AC_MSG_RESULT([$enable_tsan])

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

# Curl support for traffic_top
AC_MSG_CHECKING([whether to enable CURL])
AC_ARG_ENABLE([curl],
  [AS_HELP_STRING([--disable-curl],[turn off CURL support for traffic_top])],
  [],
  [enable_curl=yes]
)
AC_MSG_RESULT([$enable_curl])

if test "x${enable_curl}" = "xyes"; then
AX_LIB_CURL([7.19], [AC_DEFINE([HAS_CURL], [1], [Define if libcurl >= 7.19.])])
fi

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

#
# FIPS
#

AC_MSG_CHECKING([whether to enable fips])
AC_ARG_ENABLE([fips],
  [AS_HELP_STRING([--enable-fips],[turn on FIPS compliance])],
  [],
  [enable_fips=no]
)
AC_MSG_RESULT([$enable_fips])
TS_ARG_ENABLE_VAR([enable], [fips])

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
AM_CONDITIONAL([BUILD_TESTS], [test 0 -ne $has_tests])

#
# Build expensive unit tests ?
#

AC_MSG_CHECKING([whether to enable expensive unit tests])
AC_ARG_ENABLE([expensive-tests],
  [AS_HELP_STRING([--enable-expensive-tests],[turn on expensive unit tests])],
  [],
  [enable_expensive_tests=no]
)
AC_MSG_RESULT([$enable_expensive_tests])
TS_ARG_ENABLE_VAR([has], [expensive_tests])
AM_CONDITIONAL([EXPENSIVE_TESTS], [test 0 -ne $has_expensive_tests])

#
# Build documentation?
#

# Java needed only for documentation building, but making it conditional
# makes the diagnostic output ugly and hard to follow.
AC_ARG_VAR(JAVA, [path to java executor])
AC_CHECK_PROG(JAVA, java, java)
AC_MSG_RESULT([Checking whether to build documentation:])
AC_ARG_ENABLE([docs],
  [AS_HELP_STRING([--enable-docs],[enable documentation building])],
  [
    enable_doc_build=yes
    AM_PATH_PYTHON([3.4], [
      TS_MAN1_MANPAGES=`cd $srcdir/doc && $PYTHON manpages.py --section=1 | $AWK '{print "$(BUILDDIR)/man/" $0 }' | tr '\n' ' '`
      TS_MAN3_MANPAGES=`cd $srcdir/doc && $PYTHON manpages.py --section=3 | $AWK '{print "$(BUILDDIR)/man/" $0 }' | tr '\n' ' '`
      TS_MAN5_MANPAGES=`cd $srcdir/doc && $PYTHON manpages.py --section=5 | $AWK '{print "$(BUILDDIR)/man/" $0 }' | tr '\n' ' '`
      TS_MAN8_MANPAGES=`cd $srcdir/doc && $PYTHON manpages.py --section=8 | $AWK '{print "$(BUILDDIR)/man/" $0 }' | tr '\n' ' '`
    ], [
      enable_doc_build=no
      AC_MSG_ERROR(Doc building disabled, Python 3.4 or better required)
    ])

    AS_IF([test -z "$JAVA"],
      [
        enable_doc_build=no
        AC_MSG_ERROR(Doc building disabled, Java required but not found)
      ])
    AC_ARG_VAR(SPHINXBUILD, [the sphinx-build documentation generator])
    AC_ARG_VAR(SPHINXOPTS, [additional sphinx-build options])
    AC_PATH_PROG([SPHINXBUILD], [$SPHINXBUILD], [$PYTHON -m sphinx])
    AS_IF(["$PYTHON" "$srcdir/doc/checkvers.py" --check-version],
      [
        sphinx_version_check=yes
      ],[
        sphinx_version_check=no
        enable_doc_build=no
        AC_MSG_ERROR(Doc building disabled, check Sphinx installation)
      ])


    AC_SUBST(TS_MAN1_MANPAGES)
    AC_SUBST(TS_MAN3_MANPAGES)
    AC_SUBST(TS_MAN5_MANPAGES)
    AC_SUBST(TS_MAN8_MANPAGES)

    AC_MSG_CHECKING([whether to build man pages])
    AS_IF([test "x$sphinx_version_check" = "xyes" -a "x$SPHINXBUILD" != "xfalse"], [
      build_manpages=true
      AC_MSG_RESULT([yes])
    ], [
      build_manpages=false
      AC_MSG_RESULT([no])
    ])

  ],
  [enable_doc_build=no]
)
AC_MSG_RESULT([Will build documentation: $enable_doc_build])
AM_CONDITIONAL([BUILD_DOCS], [test "xyes" = "x$enable_doc_build"])
AM_CONDITIONAL([BUILD_MANPAGES], [test "xtrue" = "x$build_manpages"])

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
AM_CONDITIONAL([BUILD_WCCP], [test 0 -ne $has_wccp])

# Google profiler
AC_MSG_CHECKING([whether to enable profiler])
AC_ARG_WITH([profiler],
  [AS_HELP_STRING([--with-profiler],[enable support for profiler [default=no]])],
  [with_profiler=$withval],
  [with_profiler=no]
)
AC_MSG_RESULT([$with_profiler])

# Disable all static library builds
AC_DISABLE_STATIC

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
# Enable ccache explicitly (it's disabled by default, because of build problems in some cases)
#
AC_MSG_CHECKING([whether to enable ccache])
AC_ARG_ENABLE([ccache],
  [AS_HELP_STRING([--enable-ccache],[Enable ccache (for developers)])],
  [],
  [enable_ccache="no"]
)
AC_MSG_RESULT([$enable_ccache])

#
# Enable hardening of the executables
#
AC_MSG_CHECKING([whether to enable hardening of the executables])
AC_ARG_ENABLE([hardening],
  [AS_HELP_STRING([--enable-hardening],[Enable hardening of executables])],
  [],
  [enable_hardening="no"]
)
AC_MSG_RESULT([$enable_hardening])

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
# Experimental plugins
#

AC_MSG_CHECKING([whether to enable experimental plugins])
AC_ARG_ENABLE([experimental-plugins],
  [AS_HELP_STRING([--enable-experimental-plugins],[build experimental plugins])],
  [],
  [enable_experimental_plugins=no]
)
AC_MSG_RESULT([$enable_experimental_plugins])
AM_CONDITIONAL([BUILD_EXPERIMENTAL_PLUGINS], [ test "x${enable_experimental_plugins}" = "xyes" ])

#
# Check Magick++ is available. Enable experimental/webp_transform plugin
#
PKG_CHECK_MODULES([LIBMAGICKCPP],[Magick++ >= 7], [
  TS_ADDTO(LIBMAGICKCPP_CFLAGS, [-DMAGICK_VERSION=7])
  have_libmagickcpp=yes
  AS_IF([test "x$enable_experimental_plugins" = "xyes"], [
    enable_image_magick_plugins=yes
  ])
],
[
  PKG_CHECK_MODULES([LIBMAGICKCPP],[Magick++ < 7], [
    TS_ADDTO(LIBMAGICKCPP_CFLAGS, [-DMAGICK_VERSION=6])
    have_libmagickcpp=yes
    AS_IF([test "x$enable_experimental_plugins" = "xyes"], [
      enable_image_magick_plugins=yes
    ])
  ],
  [
    have_libmagickcpp=no
  ])])

AM_CONDITIONAL([BUILD_IMAGE_MAGICK_PLUGINS], [test "x${enable_image_magick_plugins}" = "xyes"])

#
# Example plugins. The example plugins are only built and installed if this is enabled. Installing
# them is useful for QA, but not useful for most users, so we default this to disabled.
#

AC_MSG_CHECKING([whether to install example plugins])
AC_ARG_ENABLE([example-plugins],
  [AS_HELP_STRING([--enable-example-plugins],[Build and install example plugins])],
  [],
  [enable_example_plugins=no]
)
AC_MSG_RESULT([$enable_example_plugins])
AM_CONDITIONAL([BUILD_EXAMPLE_PLUGINS], [ test "x${enable_example_plugins}" = "xyes" ])

#
# Test tools. The test tools are always built, but not always installed. Installing
# them is useful for QA, but not useful for most users, so we default this to disabled.
#

AC_MSG_CHECKING([whether to install testing tools])
AC_ARG_ENABLE([test-tools],
  [AS_HELP_STRING([--enable-test-tools],[install testing tools])],
  [],
  [enable_test_tools=no]
)
AC_MSG_RESULT([$enable_test_tools])
AM_CONDITIONAL([BUILD_TEST_TOOLS], [ test "x${enable_test_tools}" = "xyes" ])

#
# Check if we should allow builds on 32-bit platforms
#
AC_MSG_CHECKING([whether to allow 32-bit builds])
AC_ARG_ENABLE([32bit-build],
  [AS_HELP_STRING([--enable-32bit-build],[allow 32bit builds])],
  [],
  [enable_32bit=no]
)
AC_MSG_RESULT([$enable_32bit])


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
# 3) (in first kludge mode block...) obtain any further CFLAG-type additions.
# 4) Test compilers with all flags set.

# AC_PROG can sometimes mangle CFLAGS etc.
# in particular, on Linux they insert -g -O2, here we preserve any user CFLAGS
_ts_saved_CFLAGS="${CFLAGS}"
_ts_saved_CXXFLAGS="${CXXFLAGS}"

# We force the compiler search list because the default GCC on Darwin cannot build
# Traffic Server. On most (all?) platforms, cc and c++ should be the preferred default
# compiler.
AC_PROG_CC([cc gcc clang icc])
AC_PROG_CXX([c++ g++ clang++ icpc])

AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_CXXCPP
AM_PROG_AS

AX_COMPILER_VENDOR

AX_CC_FOR_BUILD

CFLAGS="${_ts_saved_CFLAGS}"
CXXFLAGS="${_ts_saved_CXXFLAGS}"

# All compilers we support have 'gnu99' as an available C standard
TS_ADDTO(AM_CFLAGS, [-std=gnu99])

ac_save_CXX="$CXX"
CXX="$CXX -std=c++17"
AC_LANG_PUSH(C++)
AC_MSG_CHECKING([whether $CXX supports -std=c++17])
AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([
#if __cplusplus < 201703L
#error "This is not C++17"
#endif
    ], []
    )], [
    AC_MSG_RESULT(yes)
    ], [
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([*** A compiler with support for -std=c++17 is required.])
])
AC_LANG_POP
CXX="$ac_save_CXX"

TS_ADDTO(AM_CXXFLAGS, [-std=c++17])

dnl AC_PROG_SED is only available from version 2.6 (released in 2003). CentosOS
dnl 5.9 still has an ancient version, but we have macros that require
dnl AC_PROG_SED. The actual AC_PROG_SED macro does functional checks, but here
dnl we define a trivial local version for times when we are running on
dnl obsoldete autoconf.
ifdef([AC_PROG_SED], [], [
  AC_DEFUN([AC_PROG_SED], [
    AC_CHECK_PROG(SED, sed, sed)
  ])
])


# Various OS specific setup. Note that on Solaris, 32-bit is always the
# default, even on a box that with 64-bit architecture.
# This also sets up a "normalized" variable and define $host_os_def.
case $host_os in
  linux*)
    host_os_def="linux"
    AM_LDFLAGS="-rdynamic"
    ;;
  darwin*)
    host_os_def="darwin"
    ;;
  freebsd*)
    host_os_def="freebsd"
    AM_LDFLAGS="-rdynamic"
    TS_ADDTO(TS_INCLUDES, [-I/usr/local/include])
    TS_ADDTO(AM_CPPFLAGS, [-D_GLIBCXX_USE_C99])
    TS_ADDTO(AM_CPPFLAGS, [-D_GLIBCXX_USE_C99_MATH])
    TS_ADDTO(AM_CPPFLAGS, [-D_GLIBCXX_USE_C99_MATH_TR1])
    ;;
  kfreebsd*)
    host_os_def="freebsd"
    AM_LDFLAGS="-rdynamic"
    TS_ADDTO(TS_INCLUDES, [-I/usr/local/include])
    TS_ADDTO(AM_CPPFLAGS, [-Dkfreebsd])
    ;;
  openbsd*)
    host_os_def="openbsd"
    ;;
  solaris*)
    host_os_def="solaris"
    case "`isalist`" in
      *amd64*)
        TS_ADDTO(AM_CFLAGS, [-m64])
        TS_ADDTO(AM_CXXFLAGS, [-m64])
        TS_ADDTO(LUAJIT_LDFLAGS, [-m64])
        ;;
    esac
    ;;
  *)
    AM_LDFLAGS="-rdynamic"
    host_os_def=unknown
    ;;
esac

TS_ADDTO(AM_CPPFLAGS, [-D$host_os_def])
AM_CONDITIONAL([OS_LINUX], [test "x$host_os_def" = "xlinux"])

dnl AM_PROG_AR is not always available, but it doesn't seem to be needed in older versions.
ifdef([AM_PROG_AR],
      [AM_PROG_AR])

AC_PROG_AWK
AC_PROG_SED
AC_PROG_LN_S
AC_PROG_INSTALL
LT_INIT
AC_CHECK_PROG(RM, rm, rm)
AC_CHECK_PROG(ASCPP, cpp, cpp)
AC_CHECK_TOOL(AR, ar, ar)
AC_SEARCH_LIBS([strerror],[cposix])

AC_ARG_VAR(RPATH, [path to be added to rpath])

AC_ARG_VAR([CLANG_TIDY], [clang-tidy command])

# Default CLANG_TIDY to "clang-tidy", or "false" if it is not present.
AC_PATH_PROG([CLANG_TIDY], [clang-tidy],[false])

# Do bison check by hand because we must do a version check.
# Use YACC because it makes autotools shut up.
BISON_MAJOR=2
BISON_MINOR=4
BISON_POINT=1
AC_CHECK_PROG([YACC],[bison],[bison])
AS_IF([test -n "$YACC"],
      [ bison_version_check=`$YACC --version 2>&1 | \
        $SED -n '/bison/s/^[[^0-9]]*\([[0-9]][[0-9.]]*\).*$/\1/p' | \
        $AWK "{ if (\\$1 > $BISON_MAJOR || (\\$1 == $BISON_MAJOR && (\\$2 > $BISON_MINOR || (\\$2 == $BISON_MINOR && (NR == 2 || \\$3 >= $BISON_POINT))))) print \"yes\"; else printf(\"version %d.%d.%d\",\\$1,\\$2,\\$3); }" FS=. \
        `
        AS_IF([test "x$bison_version_check" != "xyes"],
          [ YACC=''
            AS_IF([test -z "$bison_version_check"],
              [bison_version_check='no version data']
            )
          ]
        )
      ],
      [ YACC=''
        bison_version_check="nothing"
      ]
)
# Check lex/flex by hand because we need flex of a sufficient version.
FLEX_MAJOR=2
FLEX_MINOR=5
FLEX_POINT=33
dnl ylwrap requires the lexer executable to be an absolute path or in the srcdir.
dnl but we need various other LEX values.
AC_PROG_LEX([noyywrap])
AS_IF([test -n "$LEX"],
      [ flex_version_check=`$LEX --version 2>&1 | \
        $SED -n '/flex/s/^[[^0-9]]*\([[0-9]][[0-9.]]*\)[[^0-9]]*.*$/\1/p' | \
        $AWK "{ if (\\$1 > $FLEX_MAJOR || (\\$1 == $FLEX_MAJOR && (\\$2 > $FLEX_MINOR || (\\$2 == $FLEX_MINOR && (NR == 2 || \\$3 >= $FLEX_POINT))))) print \"yes\"; else printf(\"version %d.%d.%d\",\\$1,\\$2,\\$3); }" FS=. \
        `
        AS_IF([test "x$flex_version_check" != "xyes"],
          [ LEX=''
            AS_IF([test -z "$flex_version_check"],
              [flex_version_check='no version data']
            )
          ]
        )
      ],
      [ LEX=''
        flex_version_check="nothing"
      ]
)

# Generated files checked in, only build them if the local OS has the necessary support.
# Otherwise just use the checked in version.
AM_CONDITIONAL([BUILD_TSCONFIG_GRAMMAR], [ test -n "$LEX" && test -n "$YACC" ])

# Check for Perl and Doxygen
AC_PATH_PROG([DOXYGEN], [doxygen]) # needed for Doxygen
AC_PATH_PROG([PERL], [perl],[not found])
AS_IF([test "x$PERL" = "xnot found"],
  BUILD_PERL_LIB=false
)
AC_ARG_VAR([DOXYGEN], [full path of Doxygen executable])
AC_ARG_VAR([PERL], [full path of Perl executable])

# Check if MakeMaker is available
AX_PROG_PERL_MODULES([ExtUtils::MakeMaker], AM_CONDITIONAL([BUILD_PERL_LIB], [true]),
                                            AM_CONDITIONAL([BUILD_PERL_LIB], [false])
)

# Check for GNU-style -On optimization flags
AC_MSG_CHECKING([whether to auto-set compiler optimization flags])
has_optimizer_flags=`echo "$CFLAGS $CXXFLAGS" | ${AWK} '$0 !~ /-O.?/{print "no"}'`
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

case $host_os_def in
  linux)
    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xintel"], [
      # -Wall is overzealous for us, so need to turn this off for now:
      #
      #  #873 is "has no corresponding operator delete"
      #  #279 is "controlling expression is constant" (which is e.g. TSReleaseAssert(!"Unexpected Event");
      common_opt="-pipe -Wall -wd873 -wd279"
      debug_opt="-g $common_opt"
      release_opt="-g $common_opt $optimizing_flags -axsse4.2 -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
    ])

    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xclang"], [
      common_opt="-pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter"
      debug_opt="-ggdb3 $common_opt -Qunused-arguments"
      release_opt="-g $common_opt $optimizing_flags -fno-strict-aliasing -Qunused-arguments"
      cxx_opt="-Wno-invalid-offsetof"
    ])

    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xgnu"], [
      # This is useful for finding odd conversions
      #    common_opt="-pipe -Wall -Wconversion -Wno-sign-conversion -Wno-format-truncation"
      common_opt="-pipe -Wall -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter -Wno-format-truncation -Wno-cast-function-type -Wno-stringop-overflow"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof -Wno-noexcept-type -Wsuggest-override"
      # Special options for flex generated .c files
      flex_cflags="-Wno-unused-parameter"
    ])

    TS_ADDTO([AM_LDFLAGS], [-Wl,--as-needed])

    ;; # linux)

  darwin)
    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xclang"], [
      common_opt="-pipe -Wall -Qunused-arguments -Wextra -Wno-unused-parameter"
      debug_opt="-g $common_opt"
      release_opt="-g $common_opt $optimizing_flags -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
    ], [
      AC_MSG_WARN([clang is the only supported compiler on Darwin])
    ])

    # NOTE: This seems semi-kludgy, but useful for MacPorts I think.
    AS_IF([test -d /opt/local/include], [
      TS_ADDTO(TS_INCLUDES, [-I/opt/local/include])
    ])
    AS_IF([test -d /opt/local/lib], [
      TS_ADDTO(AM_LDFLAGS, [-L/opt/local/lib])
    ])

    ;; # darwin)

  freebsd|kfreebsd)
    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xclang"], [
      common_opt="-pipe -Wall -Wno-deprecated-declarations -Qunused-arguments -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
    ])

    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xgnu"], [
      common_opt="-pipe -Wall -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof -Wsuggest-override"
    ])

    AS_IF([test -d /usr/local/lib], [
      TS_ADDTO(AM_LDFLAGS, [-L/usr/local/lib])
    ])

    ;; # freebsd|kfreebsd)

  solaris)
    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xgnu"], [
      common_opt="-pipe -Wall -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
    ])

    ;; # solaris)

  *)
    # Not sure what platform this is, but take a stab at some general GCC options ...
    AS_IF([test "x$ax_cv_c_compiler_vendor" = "xgnu"], [
      common_opt="-pipe -Wall -Wextra -Wno-ignored-qualifiers -Wno-unused-parameter"
      debug_opt="-ggdb3 $common_opt"
      release_opt="-g $common_opt $optimizing_flags -feliminate-unused-debug-symbols -fno-strict-aliasing"
      cxx_opt="-Wno-invalid-offsetof"
    ])
esac

AS_IF([test x"$enable_coverage" = "xyes"], [
  # Map per-compiler code coverage instrumentation flags.  Note that
  # we don't test whether the compiler actually support these options
  # (we don't do that in general). The user is expected to use a modern,
  # supported compiler to test coverage.
  AS_CASE("$ax_cv_c_compiler_vendor",
    [clang], [
      TS_ADDTO(AM_CXXFLAGS, "-fprofile-instr-generate")
      TS_ADDTO(AM_CXXFLAGS, "-fcoverage-mapping")
      TS_ADDTO(AM_CFLAGS, "-fprofile-instr-generate")
      TS_ADDTO(AM_CFLAGS, "-fcoverage-mapping")
    ], [gnu], [
      TS_ADDTO(AM_CXXFLAGS, "--coverage")
      TS_ADDTO(AM_CFLAGS, "--coverage")
      TS_ADDTO(LIBS, "-lgcov")
  ])
])

# Only add -Werror if the user has requested it. We enable this by default for
# development, disable it by default for release.
AS_IF([test x"$enable_werror" = "xyes"], [
  TS_ADDTO(release_opt, -Werror)
  TS_ADDTO(debug_opt, -Werror)
])

cc_oflag_opt=$release_opt
cc_oflag_dbg=$debug_opt
cxx_oflag_opt="$release_opt $cxx_opt $cxx_rel"
cxx_oflag_dbg="$debug_opt $cxx_opt $cxx_dbg"

# Special compiler flag hacks for various pieces of the code
AC_SUBST([FLEX_CFLAGS], $flex_cflags)

#
# _Here_ is where we go ahead and add the _optimizations_ to already
#  existing CFLAGS/CXXFLAGS if some special values had been set.
#
if test "x${enable_debug}" = "xyes"; then
  TS_ADDTO(AM_CFLAGS, [${cc_oflag_dbg}])
  TS_ADDTO(AM_CXXFLAGS, [${cxx_oflag_dbg}])
  TS_ADDTO(AM_CPPFLAGS, [-DDEBUG -D_DEBUG])
else
  TS_ADDTO(AM_CFLAGS, [${cc_oflag_opt}])
  TS_ADDTO(AM_CXXFLAGS, [${cxx_oflag_opt}])
fi

if test "x${enable_mime_sanity_check}" = "xyes"; then
  TS_ADDTO(AM_CPPFLAGS, [-DENABLE_MIME_SANITY_CHECK])
fi

if test "x${enable_all_asserts}" = "xyes"; then
  TS_ADDTO(AM_CPPFLAGS, [-DENABLE_ALL_ASSERTS])
fi

if test "x${enable_event_tracker}" = "xyes"; then
  TS_ADDTO(AM_CPPFLAGS, [-DENABLE_EVENT_TRACKER])
fi

# Flags for ASAN
if test "x${enable_asan}" = "xyes"; then
  if test "x${enable_tsan}" = "xyes" -o "x${enable_tsan}" = "xstatic"; then
    AC_MSG_ERROR(Cannot have ASAN and TSAN options at the same time, pick one)
  fi
  TS_ADDTO(AM_CFLAGS, [-fno-omit-frame-pointer -fsanitize=address])
  TS_ADDTO(AM_CXXFLAGS, [-fno-omit-frame-pointer -fsanitize=address])
elif test "x${enable_asan}" = "xstatic"; then
  if test "x${enable_tsan}" = "xyes" -o "x${enable_tsan}" = "xstatic"; then
    AC_MSG_ERROR(Cannot have ASAN and TSAN options at the same time, pick one)
  fi
  asan_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -fno-omit-frame-pointer -fsanitize=address -static-libasan"
  AC_LANG_PUSH(C++)
  AC_MSG_CHECKING([static ASAN library is available])
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM([#include <stdlib.h>], [])],
    [AC_MSG_RESULT([yes])],
    [
      AC_MSG_RESULT([no])
      AC_MSG_ERROR(Cannot find static ASAN library.)
    ]
  )
  AC_LANG_POP
  CXXFLAGS="$asan_CXXFLAGS"
  TS_ADDTO(AM_CFLAGS, [-fno-omit-frame-pointer -fsanitize=address -static-libasan])
  TS_ADDTO(AM_CXXFLAGS, [-fno-omit-frame-pointer -fsanitize=address -static-libasan])
fi

# Flags for LSAN stand-alone mode
if test "x${enable_lsan}" = "xyes"; then
  if test "x${enable_asan}" = "xyes" -o "x${enable_asan}" = "xstatic"; then
    AC_MSG_ERROR(ASAN already specified, --enable-lsan is meant only for LSAN stand-alone mode)
  fi
  if test "x${enable_tsan}" = "xyes" -o "x${enable_tsan}" = "xstatic"; then
    AC_MSG_ERROR(Cannot have LSAN and TSAN options at the same time, pick one)
  fi
  TS_ADDTO(AM_CFLAGS, [-fno-omit-frame-pointer -fsanitize=leak])
  TS_ADDTO(AM_CXXFLAGS, [-fno-omit-frame-pointer -fsanitize=leak])
elif test "x${enable_lsan}" = "xstatic"; then
  if test "x${enable_asan}" = "xyes" -o "x${enable_asan}" = "xstatic"; then
    AC_MSG_ERROR(ASAN already specified, --enable-lsan is meant only for LSAN stand-alone mode)
  fi
  if test "x${enable_tsan}" = "xyes" -o "x${enable_tsan}" = "xstatic"; then
    AC_MSG_ERROR(Cannot have LSAN and TSAN options at the same time, pick one)
  fi
  AC_CHECK_LIB(lsan, _init, [lsan_have_libs=yes], [lsan_have_libs=no])
  if test "x${lsan_have_libs}" == "xno"; then
    AC_MSG_ERROR(Cannot find LSAN static library)
  fi
  lsan_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -fno-omit-frame-pointer -fsanitize=leak -static-liblsan"
  AC_LANG_PUSH(C++)
  AC_MSG_CHECKING([static LSAN library is available])
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM([#include <stdlib.h>], [])],
    [AC_MSG_RESULT([yes])],
    [
      AC_MSG_RESULT([no])
      AC_MSG_ERROR(Cannot find static LSAN library.)
    ]
  )
  AC_LANG_POP
  CXXFLAGS="$lsan_CXXFLAGS"
  TS_ADDTO(AM_CFLAGS, [-fno-omit-frame-pointer -fsanitize=leak -static-liblsan])
  TS_ADDTO(AM_CXXFLAGS, [-fno-omit-frame-pointer -fsanitize=leak -static-liblsan])
fi

# Flags for TSAN
if test "x${enable_tsan}" = "xyes"; then
  TS_ADDTO(AM_CFLAGS, [-fsanitize=thread])
  TS_ADDTO(AM_CXXFLAGS, [-fsanitize=thread])
elif test "x${enable_tsan}" = "xstatic"; then
  AC_CHECK_LIB(tsan, _init, [tsan_have_libs=yes], [tsan_have_libs=no])
  if test "x${tsan_have_libs}" == "xno"; then
    AC_MSG_ERROR(Cannot find TSAN static library)
  fi
  tsan_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -fsanitize=thread -static-libtsan"
  AC_LANG_PUSH(C++)
  AC_MSG_CHECKING([static TSAN library is available])
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM([#include <stdlib.h>], [])],
    [AC_MSG_RESULT([yes])],
    [
      AC_MSG_RESULT([no])
      AC_MSG_ERROR(Cannot find static TSAN library.)
    ]
  )
  AC_LANG_POP
  CXXFLAGS="$tsan_CXXFLAGS"
  TS_ADDTO(AM_CFLAGS, [-fsanitize=thread -static-libtsan])
  TS_ADDTO(AM_CXXFLAGS, [-fsanitize=thread -static-libtsan])
fi

# Checks for pointer size.
# TODO: Later this is irrelevant, and we should just bail on 32-bit platforms always
AC_CHECK_SIZEOF([void*])
if test "x$ac_cv_sizeof_voidp" == "x"; then
  AC_MSG_ERROR(Cannot determine size of void*)
fi

# Right now, 32-bit platform is a build error, unless we've forced it with --enable-32bit-build
if test "${ac_cv_sizeof_voidp}" = "4"; then
   AS_IF([test x"$enable_32bit_build" = "xyes"], [
     AC_MSG_NOTICE([Explicitly building on a 32-bit platform, this might be unsupported soon!])
   ], [
     AC_MSG_ERROR(You are trying to build on a 32-bit platform, which is unsupported.)
   ])
fi


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
    esac
  else
    case "$host_cpu" in
      x86_64 | amd64)
        # XXX: Any need for 64-bit arch flags?
        # cpu_architecture="-march=native"
      ;;
    esac
  fi
fi

# Override detected architecture with the user supplied one
#
AC_ARG_WITH(architecture, [AS_HELP_STRING([--with-architecture=ARCH],[use a specific CPU architecture])],
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
    AC_MSG_ERROR([--with-architecture requires a parameter])
  fi
])

if test "x$cpu_architecture" != "x"; then
  TS_ADDTO(AM_CFLAGS, [$cpu_architecture])
  TS_ADDTO(AM_CXXFLAGS, [$cpu_architecture])
fi

# 64-bit LFS support
#
TS_ADDTO(AM_CPPFLAGS, [-D_LARGEFILE64_SOURCE=1])
if test "${ac_cv_sizeof_voidp}" = "8"; then
  TS_ADDTO(AM_CPPFLAGS, [-D_COMPILE64BIT_SOURCE=1])
else
  TS_ADDTO(AM_CPPFLAGS, [-D_FILE_OFFSET_BITS=64])
fi
TS_ADDTO(CPPFLAGS, [-D_GNU_SOURCE])
TS_ADDTO(AM_CPPFLAGS, [-D_REENTRANT])
TS_ADDTO(AM_CPPFLAGS, [-D__STDC_LIMIT_MACROS=1])
TS_ADDTO(AM_CPPFLAGS, [-D__STDC_FORMAT_MACROS=1])

AC_MSG_NOTICE([Build for host OS: $host_os, arch: $host_cpu, optimization: $host_os_def])

# Add hardening options to flags
AS_IF([test "x${enable_hardening}" = "xyes"], [
  TS_ADDTO(AM_CPPFLAGS, [-D_FORTIFY_SOURCE=2])
  TS_ADDTO(AM_CXXFLAGS, [-fPIE -fstack-protector])
  TS_ADDTO(AM_CFLAGS, [-fPIE -fstack-protector])
  AS_CASE("$host_os_def",
    [linux], [TS_ADDTO(AM_LDFLAGS, [-pie -Wl,-z,relro -Wl,-z,now])]
  )
])

#
# Note:  These are site-specific macro's that do various tests
#         on the selected compilers.  There was some tuning
#         associated with our not wanting to use GNU for _everything_.
# Note:  This macro may set certain parameters when run.
#

# Check for ccache (if explicitly enabled)
if test "x$enable_ccache" = "xyes"; then
    AC_CHECK_PROG([CCACHE],[ccache],[ccache],[])
    if test "x${CCACHE}" = "xccache"; then
        CC="$CCACHE $CC"
        CXX="$CCACHE $CXX"
    fi
fi

# -----------------------------------------------------------------------------
# 4. CHECK FOR LIBRARIES

AC_SEARCH_LIBS([socket], [socket], [], [])
AC_SEARCH_LIBS([gethostbyname], [nsl], [], [])
AC_SEARCH_LIBS([clock_gettime], [rt posix4], [], [])

dnl We check for dlsym here instead of e.g. dlopen() because ASAN hijacks the latter.
AC_SEARCH_LIBS([dlsym], [dl], [], [])

dnl Linux has pthread symbol stubss in both libc and libpthread, so it's important to test
dnl specifically for pthread_yield() here. In addition, ASAN hijacks pthread_create() so
dnl we can't use that anymore.
AC_SEARCH_LIBS([pthread_yield], [pthread], [], [])
AC_CHECK_FUNCS([pthread_mutexattr_settype])

dnl XXX The following check incorrectly causes the build to succeed
dnl on Darwin. We should be using AC_SEARCH_LIBS, but rest_init is
dnl actually present in libsystem. We are searching for the library
dnl that contains the full Bind 9 API (which is actually libresolv).
dnl However, the resolv API uses macros to rename it's APIs to per-version
dnl symbols, so standard autoconf macros cannot reasonably be used to
dnl check for it. We need to write custom macros to detect it properly.
AC_CHECK_LIB([resolv],[res_init],[AC_SUBST([LIBRESOLV],["-lresolv"])])
AC_CHECK_LIB([resolv],[__putlong],[AC_SUBST([LIBRESOLV],["-lresolv"])])

# Test for ncurses. We need to turn off -Werror because the C code in the
# ncurses compile tests does not generate unused variable warnings.
__saved_CFLAGS="$CFLAGS"
TS_REMOVEFROM(CFLAGS, -Werror)

dnl Red Hat 6 requires special flags for curses to work.
if test -r /etc/system-release ; then
  case `cat /etc/system-release` in
    Red\ Hat*release\ 6.*)
      TS_ADDTO(CFLAGS, [-Wl,--add-needed])
      curses_ldflags="-Wl,--add-needed"
    ;;
  esac
fi

AX_WITH_CURSES
CFLAGS="$__saved_CFLAGS"
AC_SUBST([CURSES_LDFLAGS],[$curses_ldflags])

#
# Check for -latomic need (at least for mips arch)
TS_CHECK_ATOMIC
TS_ADDTO([LDFLAGS], [$ATOMIC_LIBS])

#
# Check for SSL presence and usability
#
TS_CHECK_CRYPTO

# Check for OpenSSL Version
TS_CHECK_CRYPTO_VERSION

# Check for OpenSSL Version 3 and add compatiblity define if needed
TS_CHECK_OPENSSL3
AM_CONDITIONAL([OPENSSL_IS_OPENSSL3], [test -n "$openssl_is_openssl3"])

# Check for openssl ASYNC jobs
TS_CHECK_CRYPTO_ASYNC

# Check for the client hello callback
TS_CHECK_CRYPTO_HELLO_CB

# Check for SSL_set0_rbio call
TS_CHECK_CRYPTO_SET_RBIO

# Check for DH_get_2048_256
TS_CHECK_CRYPTO_DH_GET_2048_256

# Check for HKDF support
TS_CHECK_CRYPTO_HKDF
AM_CONDITIONAL([HAS_HKDF], [test "x$enable_hkdf" = "xyes"])

# Check for TLS 1.3 support
TS_CHECK_CRYPTO_TLS13

# Check for QUIC support
enable_quic=no
AC_MSG_CHECKING([whether APIs for QUIC are available])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <openssl/ssl.h>]],
                                   [[
                                     SSL_QUIC_METHOD var;
                                   ]])
                  ],
                  [
                    AC_MSG_RESULT([yes])
                    enable_quic=yes
                    _quic_saved_LIBS=$LIBS
                    TS_ADDTO(LIBS, [$OPENSSL_LIBS])
                    AC_CHECK_FUNCS(SSL_set_quic_early_data_enabled)
                    LIBS=$_quic_saved_LIBS
                  ],
                  [
                    AC_MSG_RESULT([no])
                  ])

AM_CONDITIONAL([ENABLE_QUIC], [test "x$enable_quic" = "xyes"])
TS_ARG_ENABLE_VAR([use], [quic])
AC_SUBST(use_quic)

# Check for OCSP
TS_CHECK_CRYPTO_OCSP

# Check for SSL_CTX_set_ciphersuites call
TS_CHECK_CRYPTO_SET_CIPHERSUITES

# Check for TOLS keylogging support.
TS_CHECK_CRYPTO_KEYLOGGING

# Check for openssl early data support
TS_CHECK_EARLY_DATA

# Check for openssl session ticket support
TS_CHECK_SESSION_TICKET

# Check for openssl verify cert store support
TS_CHECK_VERIFY_CERT_STORE

saved_LIBS="$LIBS"
TS_ADDTO([LIBS], ["$OPENSSL_LIBS"])

AC_CHECK_FUNCS([ \
  BIO_meth_new \
  BIO_sock_non_fatal_error \
  CRYPTO_set_mem_functions \
  HMAC_CTX_new \
  X509_get0_signature \
  ERR_get_error_all \
  SHA1 \
  SSL_SESSION_dup \
])

AC_CHECK_FUNC([ASN1_STRING_get0_data], [],
              [AC_DEFINE([ASN1_STRING_get0_data], [ASN1_STRING_data], [Added in OpenSSL 1.1])])

AC_CHECK_FUNC([BIO_set_data], [],
              [AC_DEFINE([BIO_set_data(a, _ptr)], [((a)->ptr = (_ptr))], [Added in OpenSSL 1.1])])
AC_CHECK_FUNC([BIO_get_data], [],
              [AC_DEFINE([BIO_get_data(a)], [((a)->ptr)], [Added in OpenSSL 1.1])])
AC_CHECK_FUNC([BIO_get_shutdown], [],
              [AC_DEFINE([BIO_get_shutdown(a)], [((a)->shutdown)], [Added in OpenSSL 1.1])])
AC_CHECK_FUNC([BIO_meth_get_ctrl], [],
              [AC_DEFINE([BIO_meth_get_ctrl(biom)], [((biom)->ctrl)], [Added in OpenSSL 1.1])])
AC_CHECK_FUNC([BIO_meth_get_create], [],
              [AC_DEFINE([BIO_meth_get_create(biom)], [((biom)->create)], [Added in OpenSSL 1.1])])
AC_CHECK_FUNC([BIO_meth_get_destroy], [],
              [AC_DEFINE([BIO_meth_get_destroy(biom)], [((biom)->destroy)], [Added in OpenSSL 1.1])])

AC_CHECK_FUNC([EVP_MD_CTX_new], [],
              [AC_DEFINE([EVP_MD_CTX_new], [EVP_MD_CTX_create], [Renamed in OpenSSL 1.1])])
AC_CHECK_FUNC([EVP_MD_CTX_reset], [],
              [AC_DEFINE([EVP_MD_CTX_reset], [EVP_MD_CTX_cleanup], [Renamed in OpenSSL 1.1])])
AC_CHECK_FUNC([EVP_MD_CTX_free], [],
              [AC_DEFINE([EVP_MD_CTX_free], [EVP_MD_CTX_destroy], [Renamed in OpenSSL 1.1])])

AC_MSG_CHECKING([for OpenSSL is BoringSSL])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <openssl/base.h>]],
                                   [[
                                     #ifndef OPENSSL_IS_BORINGSSL
                                     # error not boringssl
                                     #endif
                                   ]])
                  ],
                  [AC_MSG_RESULT([yes]); openssl_is_boringssl=1],
                  [AC_MSG_RESULT([no])])
AM_CONDITIONAL([OPENSSL_IS_BORINGSSL], [test -n "$openssl_is_boringssl"])

LIBS="$saved_LIBS"

#
# Check OpenSSL version for JA3 Fingerprint
#
AC_MSG_CHECKING([for JA3 compatible OpenSSL version])
AC_EGREP_CPP(yes, [
  #include <openssl/opensslv.h>
  #if (OPENSSL_VERSION_NUMBER < 0x010100000L)
  yes
  #elif (OPENSSL_VERSION_NUMBER >= 0x010101000L)
  yes
  #endif
  ], [
    AC_MSG_RESULT(yes)
    AS_IF([test "x${enable_experimental_plugins}" = "xyes" && test -z "$openssl_is_boringssl"], [
      enable_ja3_plugin=yes
    ])
  ], [AC_MSG_RESULT(no)])

AM_CONDITIONAL([BUILD_JA3_PLUGIN], [test "x${enable_ja3_plugin}" = "xyes"])

#
# Check for zlib presence and usability
TS_CHECK_ZLIB
if test "x${enable_zlib}" != "xyes"; then
  AC_MSG_ERROR([Cannot find zlib library. Configure --with-zlib=DIR])
fi

#
# Check for lzma presence and usability
TS_CHECK_LZMA

AC_CHECK_FUNCS([clock_gettime kqueue epoll_ctl posix_fadvise posix_madvise posix_fallocate inotify_init])
AC_CHECK_FUNCS([port_create strlcpy strlcat sysconf sysctlbyname getpagesize])
AC_CHECK_FUNCS([getreuid getresuid getresgid setreuid setresuid getpeereid getpeerucred])
AC_CHECK_FUNCS([strsignal psignal psiginfo accept4])
AC_CHECK_FUNCS([sendmmsg])

# Check for eventfd() and sys/eventfd.h (both must exist ...)
AC_CHECK_HEADERS([sys/eventfd.h], [
  AS_IF([test "x$enable_eventfd" = "xyes"], [
    AC_CHECK_FUNCS([eventfd])
  ]
)])

AC_CHECK_FUNCS(eventfd)

#
# Check for mcheck_pedantic(3)
#
AC_CHECK_HEADERS(mcheck.h)
AC_CHECK_FUNCS(mcheck_pedantic)

#
# Check for malloc_usable_size()
#
AC_CHECK_FUNCS(malloc_usable_size)

#
# Check for pcre library
#
TS_CHECK_PCRE
if test "x${enable_pcre}" != "xyes"; then
  AC_MSG_ERROR([Cannot find pcre library. Configure --with-pcre=DIR])
fi

# Check for optional brotli library
TS_CHECK_BROTLI

# Check for optional luajit library
TS_CHECK_LUAJIT

# Check for optional quiche library
TS_CHECK_QUICHE
if test "${has_quiche}" = "1"; then
enable_quic=yes
## Doing these again for Quiche
AM_CONDITIONAL([ENABLE_QUIC], [test "x$enable_quic" = "xyes"])
TS_ARG_ENABLE_VAR([use], [quic])
AC_SUBST(use_quic)
fi

# Check for optional WAVM library
TS_CHECK_WAVM

#
# Enable experimental/uri_signing plugin
# This is here, instead of above, because it needs to know if PCRE is available.
#

#### Check for optional jansson library (uri_signing)
TS_CHECK_JANSSON

AC_CHECK_LIB([crypto],[HMAC],[has_libcrypto=1],[has_libcrypto=0])

#### Check for optional cjose library (uri_signing)
TS_CHECK_CJOSE

AM_CONDITIONAL([BUILD_URI_SIGNING_PLUGIN], [test ! -z "${LIBCJOSE}" -a ! -z "${LIBJANSSON}" -a "x${enable_pcre}" = "xyes" -a "x${has_libcrypto}" = "x1"])
AC_SUBST([LIBCJOSE])
AC_SUBST([LIBJANSSON])

# Check for yaml-cpp library
#
TS_CHECK_YAML_CPP
AM_CONDITIONAL([BUILD_YAML_CPP], [test x"$has_yaml_cpp" = x"no"])

TS_CHECK_YAML_HEADERS_EXPORT
AM_CONDITIONAL([EXPORT_YAML_HEADERS], [test x"$enable_yaml_headers" = x"yes"])

TS_CHECK_LIBSWOC
AM_CONDITIONAL([BUILD_SWOC], [test x"$has_libswoc" = x"no"])

TS_CHECK_SWOC_HEADERS_EXPORT
AM_CONDITIONAL([EXPORT_SWOC_HEADERS], [test x"$enable_swoc_headers" = x"yes"])

# Check for optional boringocsp library
TS_CHECK_BORINGOCSP

# Check for optional hiredis library
TS_CHECK_HIREDIS
AM_CONDITIONAL([BUILD_SSL_SESSION_REUSE_PLUGIN], [test ! -z "${LIB_HIREDIS}" -a "x${has_hiredis}" = "x1" ])

# Check for optional nuraft library
TS_CHECK_NURAFT
AM_CONDITIONAL([BUILD_STEK_SHARE_PLUGIN], [test x"$has_nuraft" = x"yes"])

# Check for backtrace() support
has_backtrace=0
AC_CHECK_HEADERS([execinfo.h], [has_backtrace=1],[])
if test "${has_backtrace}" = "1"; then
  # FreeBSD requires '/usr/ports/devel/libexecinfo' for gdb style backtrace() support
  AC_SEARCH_LIBS([backtrace], [execinfo], [have_backtrace_lib=yes])
else
  AC_MSG_WARN([No backtrace() support found])
fi
AC_SUBST(has_backtrace)

#
# use unwind library when possible (can be disabled)
#
AC_MSG_CHECKING([whether to use unwind library])
AC_ARG_ENABLE([unwind],
  AS_HELP_STRING([--disable-unwind],[Don't use the unwind library]), [
  ], [
    enable_unwind="yes"
    enable_unwind_default="yes"
])
AC_MSG_RESULT([$enable_unwind])

AS_IF([test "x$enable_unwind" = "xyes"], [
  # Remote process unwinding is only implemented on Linux because it depends on various Linux-specific
  # features such as /proc filesystem nodes, ptrace(2) and waitpid(2) extensions.
  AS_IF([test "$host_os_def" = "linux"], [
    PKG_CHECK_MODULES([LIBUNWIND], [libunwind-ptrace], [
      enable_remote_unwinding=yes
    ], [
      AS_IF([test "x$enable_unwind_default" = "xyes"], [
        AC_MSG_WARN([unwind not found, try disabling it --disable-unwind])
      ], [
        AC_MSG_ERROR([unwind not found, try disabling it --disable-unwind])
      ])
    ])], [
    AS_IF([test "x$enable_unwind_default" = "xyes"], [
      AC_MSG_WARN([unwind only available on Linux, try disabling it --disable-unwind])
    ], [
      AC_MSG_ERROR([unwind only available on Linux, try disabling it --disable-unwind])
    ])
  ])
])
TS_ARG_ENABLE_VAR([use], [remote_unwinding])

# Find the appropriate event handling interface. This can be forced on
# platforms that support 2 or more of our supported interfaces. It
# could also (in the future?) be used to enable other event systems
# such as libev.
AC_ARG_WITH([event-interface],
  [AS_HELP_STRING([--with-event-interface=epoll|kqueue|port],[event interface to use [default=auto]])],
  [event_interface=$withval],
  [event_interface="auto"]
)

use_epoll=0
use_kqueue=0
use_port=0

AS_IF([test "x$event_interface" = "xauto"], [
  if test "$ac_cv_func_port_create" = "yes"; then
    use_port=1
    have_good_poller=1
    AC_MSG_NOTICE([Using port event interface])
  elif test "$ac_cv_func_epoll_ctl" = "yes"; then
    use_epoll=1
    have_good_poller=1
    AC_MSG_NOTICE([Using epoll event interface])
  elif test "$ac_cv_func_kqueue" = "yes"; then
    use_kqueue=1
    have_good_poller=1
    AC_MSG_NOTICE([Using kqueue event interface])
  else
    AC_MSG_FAILURE([No suitable polling interface found])
  fi
],[
  case "x$event_interface" in
    xepoll)
      use_epoll=1
      AC_MSG_RESULT([forced to epoll])
      ;;
    xport)
      use_port=1
      AC_MSG_RESULT([forced to port])
      ;;
    xkqueue)
      use_kqueue=1
      AC_MSG_RESULT([forced to kqueue])
      ;;
    *)
      AC_MSG_RESULT([failed])
      AC_MSG_FAILURE([unknown event system])
  esac
])

AC_SUBST(use_epoll)
AC_SUBST(use_kqueue)
AC_SUBST(use_port)


# Profiler support
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

AC_MSG_CHECKING(for 128bit CAS support)
AC_LANG_PUSH([C++])

# We need to save and restore compiler flags around this whole block.
# TS_TRY_COMPILE_NO_WARNING will save and restore flags, so if we do that in the
# middle, then we can accidentally restore modified flags.
__saved_CXXFLAGS="${CXXFLAGS}"
__saved_CFLAGS="${CFLAGS}"

has_128bit_cas=0
# Don't add the -mcx16 flag unless needed and it compiles cleanly.
needs_mcx16_for_cas=0

TS_TRY_COMPILE_NO_WARNING([],[
    __int128_t x = 0;
    __sync_bool_compare_and_swap(&x,0,10);
  ], [
    AC_MSG_RESULT(yes)
    has_128bit_cas=1
  ], [
    dnl If 128bit CAS fails, try again with the -mcx16 option. GCC needs this;
    dnl clang doesn't; icc does not support -mcx16 (but gives a non-fatal warning).
    TS_ADDTO(CXXFLAGS, [-mcx16])
    TS_ADDTO(CFLAGS, [-mcx16])
    TS_TRY_COMPILE_NO_WARNING([],[
        __int128_t x = 0;
        __sync_bool_compare_and_swap(&x,0,10);
      ], [
        AC_MSG_RESULT(yes)
        has_128bit_cas=1
        needs_mcx16_for_cas=1
      ], [
        AC_MSG_RESULT(no)
    ])
])

CXXFLAGS="${__saved_CXXFLAGS}"
CFLAGS="${__saved_CFLAGS}"
AC_LANG_POP
AC_SUBST(has_128bit_cas)

AS_IF([test "x$needs_mcx16_for_cas" = "x1"], [
    TS_ADDTO(AM_CFLAGS, [-mcx16])
    TS_ADDTO(AM_CXXFLAGS, [-mcx16])
])

# Check for POSIX capabilities library.
# If we don't find it, disable checking for header.
use_posix_cap=0
AS_IF([test "x$enable_posix_cap" != "xno"],
  AC_CHECK_LIB([cap], [cap_set_proc],
    [AC_SUBST([LIBCAP], ["-lcap"])
     use_posix_cap=1
    ],[
     AS_IF([test "x$enable_posix_cap" == "xyes"], [
      AC_MSG_FAILURE([POSIX capabilities enabled but system library not found.])
     ],[
      [enable_posix_cap=no]
    ]    )
   ]
  )
)
AC_SUBST(use_posix_cap)

#
# If the OS is linux, we can use the '--enable-experimental-linux-native-aio' option to
# replace the aio thread mode. Effective only on the linux system.
#

AC_MSG_CHECKING([whether to enable Linux native AIO])
AC_ARG_ENABLE([experimental-linux-native-aio],
  [AS_HELP_STRING([--enable-experimental-linux-native-aio], [WARNING this is experimental and has known issues enable native Linux AIO support @<:@default=no@:>@])],
  [enable_linux_native_aio="${enableval}"],
  [enable_linux_native_aio=no]
)

AS_IF([test "x$enable_linux_native_aio" = "xyes"], [
  if test $host_os_def  != "linux"; then
    AC_MSG_ERROR([Linux native AIO can only be enabled on Linux systems])
  fi

  AC_CHECK_HEADERS([libaio.h], [],
    [AC_MSG_ERROR([Linux native AIO requires libaio.h])]
  )

  AC_SEARCH_LIBS([io_submit], [aio], [],
    [AC_MSG_ERROR([Linux native AIO requires libaio])]
  )
])

AC_MSG_RESULT([$enable_linux_native_aio])
TS_ARG_ENABLE_VAR([use], [linux_native_aio])

# Check for enabling io_uring on linux

AC_MSG_CHECKING([whether to enable Linux io_uring])
AC_ARG_ENABLE([experimental-linux-io-uring],
  [AS_HELP_STRING([--enable-experimental-linux-io-uring], [WARNING this is experimental: enable Linux io_uring support @<:@default=no@:>@])],
  [enable_linux_io_uring="${enableval}"],
  [enable_linux_io_uring=no]
)

AS_IF([test "x$enable_linux_io_uring" = "xyes"], [
  URING_LIBS="-luring"
  if test $host_os_def  != "linux"; then
    AC_MSG_ERROR([Linux io_uring can only be enabled on Linux systems])
  fi

  AC_CHECK_HEADERS([liburing.h], [],
    [AC_MSG_ERROR([Linux io_uring requires liburing.h (install liburing)])]
  )

  AC_SEARCH_LIBS([io_uring_queue_init], [uring], [AC_SUBST([URING_LIBS])],
    [AC_MSG_ERROR([Linux io_uring require uring])]
  )
])

AC_MSG_RESULT([$enable_linux_io_uring])
TS_ARG_ENABLE_VAR([use], [linux_io_uring])


# Check for hwloc library.
# If we don't find it, disable checking for header.
use_hwloc=0
AS_IF([test "x$enable_hwloc" = "xyes"], [
  # Use pkg-config, because some distros (*cough* Ubuntu) put hwloc in unusual places.
  PKG_CHECK_MODULES([HWLOC], [hwloc], [
    SAVE_LIBS="$LIBS"
    LIBS="-lhwloc"
    AC_LANG_PUSH([C++])
    AC_MSG_CHECKING([for hwloc C++ linking])
    AC_LINK_IFELSE([
      AC_LANG_PROGRAM([#include <hwloc.h>],[hwloc_topology_t t; hwloc_topology_init(&t); hwloc_get_type_depth(t, HWLOC_OBJ_SOCKET);])],[
      use_hwloc=1
      AC_SUBST([HWLOC_CFLAGS])
      AC_SUBST([HWLOC_LIBS])
      AC_MSG_RESULT([yes])
      # Old versions of libhwloc don't have HWLOC_OBJ_PU.
      AC_CHECK_DECL(HWLOC_OBJ_PU,
        [AC_DEFINE(HAVE_HWLOC_OBJ_PU, 1, [Whether HWLOC_OBJ_PU is available])], [],
        [#include <hwloc.h>]
      )
    ], [
      AC_MSG_RESULT([no])
      AC_MSG_WARN([hwloc not linkable, try --disable-hwloc])
      AC_SUBST([HWLOC_CFLAGS],[""])
      AC_SUBST([HWLOC_LIBS],[""])
    ])
    AC_LANG_POP()
    LIBS="$SAVE_LIBS"
  ], [
    AC_MSG_WARN([hwloc not found, try --disable-hwloc])
    AC_SUBST([HWLOC_CFLAGS],[""])
    AC_SUBST([HWLOC_LIBS],[""])
  ])
])

AC_SUBST(use_hwloc)

#
# Check for Maxmind APIs / includes. TODO: Long term, it might make sense to support
# GeoIP as a "helper" plugin, which other plugins can then use. Such a plugin could
# then manage which libraries to use via explicit dlopen()'s.
#
AC_CHECK_HEADERS([GeoIP.h], [
  AC_CHECK_LIB([GeoIP], [GeoIP_new], [
    AC_SUBST([GEOIP_LIBS], ["-lGeoIP"])
    AC_SUBST(has_geoip, 1)
  ], [
    AC_SUBST([GEOIP_LIBS], [""])
    AC_SUBST(has_geoip, 0)
  ])
])

AM_CONDITIONAL([HAS_GEOIP], [test "x${has_geoip}" = "x1" ])

#
# Check for libmaxmind.  This is the maxmind v2 API where GeoIP is the legacy
# v1 dat file based API
#
AC_CHECK_HEADERS([maxminddb.h], [
  AC_CHECK_LIB([maxminddb], [MMDB_open], [
    AC_SUBST([MAXMINDDB_LIBS], ["-lmaxminddb"])
    AC_SUBST(has_maxminddb, 1)
  ], [
    AC_SUBST([MAXMINDDB_LIBS], [""])
    AC_SUBST(has_maxminddb, 0)
  ])
])

AM_CONDITIONAL([HAS_MAXMINDDB], [test "x${has_maxminddb}" = "x1" ])

AC_ARG_WITH([hrw-geo-provider],
  [AS_HELP_STRING([--with-hrw-geo-provider=geoip|maxminddb],[geo provider to use with header_rewrite [default=auto] ])],
  [geo_provider=$withval],
  [geo_provider="auto"]
)
use_hrw_geoip=0
use_hrw_maxminddb=0

AS_IF([test "x$geo_provider" = "xauto"], [
  if test "x$has_geoip" = "x1"; then
    use_hrw_geoip=1
    AC_MSG_NOTICE([Using GeoIP interface for header_rewrite])
  elif test "x$has_maxminddb" = "x1"; then
    use_hrw_maxminddb=1
    AC_MSG_NOTICE([Using MaxMindDB interface for header_rewrite])
  fi
],[
  case "x$geo_provider" in
    xgeoip)
      use_hrw_geoip=1
      AC_MSG_RESULT([forced to GeoIP])
      ;;
    xmaxminddb)
      use_hrw_maxminddb=1
      AC_MSG_RESULT([forced to MaxMindDB])
      ;;
    *)
      AC_MSG_RESULT([failed])
      AC_MSG_FAILURE([unknown geo interface $geo_provider])
  esac
])

AC_SUBST(use_hrw_geoip)
AC_SUBST(use_hrw_maxminddb)

# Checking if opentelemetry OTLP is available
AC_LANG_PUSH([C++])
AC_CHECK_HEADERS([opentelemetry/trace/provider.h], [
  AC_CHECK_LIB([curl], [curl_version], [
    AC_CHECK_LIB([protobuf], [main], [
      AC_CHECK_LIB([opentelemetry_exporter_otlp_http], [main], [
        AC_SUBST([OTEL_LIBS], [" -lopentelemetry_exporter_otlp_http -lopentelemetry_exporter_otlp_http_client -lopentelemetry_otlp_recordable -lopentelemetry_proto -lopentelemetry_trace -lopentelemetry_resources -lopentelemetry_version -lopentelemetry_common -lopentelemetry_http_client_curl -lcurl -lprotobuf "])
        AC_SUBST(has_otel, 1)
      ], [
        AC_SUBST([OTEL_LIBS], [""])
        AC_SUBST(has_otel, 0)
      ])
    ], [
      AC_SUBST([OTEL_LIBS], [""])
      AC_SUBST(has_otel, 0)
    ])
  ], [
    AC_SUBST([OTEL_LIBS], [""])
    AC_SUBST(has_otel, 0)
  ])
])

AM_CONDITIONAL([HAS_OTEL], [test "x${has_otel}" = "x1" ])
AC_LANG_POP([C++])

# Checking if wamr is available
AC_LANG_PUSH([C++])
AC_CHECK_HEADERS([wasm_c_api.h], [
  AC_CHECK_LIB([iwasm], [main], [
    AC_SUBST([WAMR_LIBS], [" -liwasm "])
    AC_SUBST(has_wamr, 1)
  ], [
    AC_SUBST([WAMR_LIBS], [""])
    AC_SUBST(has_wamr, 0)
  ])
])

AM_CONDITIONAL([HAS_WAMR], [test "x${has_wamr}" = "x1" ])
AC_LANG_POP([C++])

# Right now, the healthcheck plugins requires inotify_init (and friends)
AM_CONDITIONAL([BUILD_HEALTHCHECK_PLUGIN], [ test "$ac_cv_func_inotify_init" = "yes" ])

#
# Check for tcmalloc, jemalloc and mimalloc
TS_CHECK_TCMALLOC
TS_CHECK_JEMALLOC
TS_CHECK_MIMALLOC

#
# Check whether to use malloc or freelist allocator
AC_MSG_CHECKING([Use malloc allocator])
AC_ARG_ENABLE([malloc-allocator],
  [AS_HELP_STRING([--enable-malloc-allocator],[Allocator uses malloc])],
  [enable_malloc_allocator=1],
  [enable_malloc_allocator=0]
)
AC_MSG_RESULT([$enable_malloc_allocator])
AC_SUBST(enable_malloc_allocator)

# We should be able to build http_load if epoll(2) is available.
AM_CONDITIONAL([BUILD_HTTP_LOAD], [test x"$ac_cv_func_epoll_ctl" = x"yes"])

# We should only build traffic_top if we have curses
AM_CONDITIONAL([BUILD_TRAFFIC_TOP], [test "x$ax_cv_curses" = "xyes"])

AC_CHECK_HEADERS([kclangc.h], [
  AC_CHECK_LIB([kyotocabinet], [kcdbopen], [
    AC_SUBST([LIB_KYOTOCABINET], ["-lkyotocabinet"])
    has_kyotocabinet=1
  ], [
    has_kyotocabinet=0
  ])
],
[has_kyotocabinet=0]
)
AC_SUBST(has_kyotocabinet)
AM_CONDITIONAL([HAS_KYOTOCABINET], [ test "x${has_kyotocabinet}" = "x1" ])

# -----------------------------------------------------------------------------
# 5. CHECK FOR HEADER FILES

AC_CHECK_HEADERS([sys/types.h \
                  sys/uio.h \
                  sys/mman.h \
                  sys/epoll.h \
                  sys/event.h \
                  sys/param.h \
                  sys/pset.h \
                  sched.h \
                  pthread.h \
                  sys/endian.h \
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
                  float.h \
                  libgen.h \
                  values.h \
                  alloca.h \
                  cpio.h \
                  stropts.h \
                  sys/param.h \
                  sys/sysmacros.h \
                  stdint.h \
                  stdbool.h \
                  sysexits.h \
                  net/ppp_defs.h \
                  ifaddrs.h\
                  ucred.h ])

# On OpenBSD, pthread.h must be included before pthread_np.h
AC_CHECK_HEADERS([pthread_np.h], [], [], [#include <pthread.h>])
AC_CHECK_HEADERS([sys/statfs.h sys/statvfs.h sys/disk.h sys/disklabel.h sys/mount.h])
AC_CHECK_HEADERS([linux/hdreg.h linux/major.h])

AC_CHECK_HEADERS([sys/sysctl.h], [], [],
                 [[#ifdef HAVE_SYS_PARAM_H
                    #include <sys/param.h>
                   #endif
                 ]])

AC_CHECK_HEADERS([sys/cpuset.h], [], [],
                 [[#ifdef HAVE_SYS_PARAM_H
                    #include <sys/param.h>
                   #endif
                 ]])

AC_CHECK_HEADERS([sys/mount.h], [], [],
                 [[#ifdef HAVE_SYS_PARAM_H
                    #include <sys/param.h>
                   #endif
                 ]])

AC_CHECK_HEADERS([arpa/inet.h], [], [],
                 [[#ifdef HAVE_SYS_TYPES_H
                    #include <sys/types.h>
                   #endif
                   #ifdef HAVE_NETINET_IN_H
                    #include <netinet/in.h>
                   #endif
                 ]])

AC_CHECK_HEADERS([netinet/ip.h], [], [],
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

AC_CHECK_HEADERS([netinet/ip_icmp.h], [], [],
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


# Test for additional pthread interfaces.

# Darwin pthread_setname_np:
AC_MSG_CHECKING([for 1-parameter version of pthread_setname_np()])
AC_LINK_IFELSE([
    AC_LANG_PROGRAM([
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif
#if PTHREAD_NP_H
#include <pthread_np.h>
#endif
      ], [
        pthread_setname_np("conftest");
    ])
  ], [
    AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_1, 1, [Whether the 1 parameter version of pthread_setname_np() is available])
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no])
])

# Linux pthread_setname_np:
AC_MSG_CHECKING([for 2-parameter version of pthread_setname_np()])
AC_LINK_IFELSE([
    AC_LANG_PROGRAM([
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif
#if PTHREAD_NP_H
#include <pthread_np.h>
#endif
      ], [
        pthread_setname_np(pthread_self(), "conftest");
    ])
  ], [
    AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_2, 1, [Whether the 2 parameter version of pthread_setname_np() is available])
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no])
])

# BSD pthread_set_name_np:
AC_MSG_CHECKING([for 2-parameter version of pthread_set_name_np()])
AC_LINK_IFELSE([
    AC_LANG_PROGRAM([
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif
#if PTHREAD_NP_H
#include <pthread_np.h>
#endif
      ], [
        pthread_set_name_np(pthread_self(), "conftest");
    ])
  ], [
    AC_DEFINE(HAVE_PTHREAD_SET_NAME_NP_2, 1, [Whether the 2 parameter version of pthread_set_name_np() is available])
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no])
])

# pthread_getname_np / pthread_get_name_np:
AC_MSG_CHECKING([pthread_getname_np()])
AC_LINK_IFELSE([
    AC_LANG_PROGRAM([
#if HAVE_PTHREAD_H
#include <pthread.h>
#endif
#if PTHREAD_NP_H
#include <pthread_np.h>
#endif
      ], [
        char name[[32]];
        pthread_getname_np(pthread_self(), name, sizeof(name));
    ])
  ], [
    AC_DEFINE(HAVE_PTHREAD_GETNAME_NP, 1, [Whether pthread_getname_np() is available])
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no])
    AC_MSG_CHECKING([pthread_get_name_np()])
    AC_LINK_IFELSE([
        AC_LANG_PROGRAM([
    #if HAVE_PTHREAD_H
    #include <pthread.h>
    #endif
    #if PTHREAD_NP_H
    #include <pthread_np.h>
    #endif
          ], [
            char name[[32]];
            pthread_get_name_np(pthread_self(), name, sizeof(name));
        ])
      ], [
        AC_DEFINE(HAVE_PTHREAD_GET_NAME_NP, 1, [Whether pthread_get_name_np() is available])
        AC_MSG_RESULT([yes])
      ], [
        AC_MSG_RESULT([no])
    ])
])

# BSD-derived systems populate the socket length in the structure itself. It's
# redundant to check all of these, but hey, I need the typing practice. Also, we
# check for the linux updated version of tcp.h, in linux/tcp.h
AC_CHECK_MEMBER([struct sockaddr.sa_len], [], [], [#include <netinet/in.h>])
AC_CHECK_MEMBER([struct sockaddr_in.sin_len], [], [], [#include <netinet/in.h>])
AC_CHECK_MEMBER([struct sockaddr_in6.sin6_len], [], [], [#include <netinet/in.h>])
AC_CHECK_MEMBER([struct tcp_info.tcpi_data_segs_out], [], [], [#include <linux/tcp.h>])

if test "x${ac_cv_member_struct_sockaddr_sa_len}" = "xyes"; then
    AC_DEFINE(HAVE_STRUCT_SOCKADDR_SA_LEN, 1,
            [Whether struct sockaddr_in has the sa_len member])
fi

if test "x${ac_cv_member_struct_sockaddr_in_sin_len}" = "xyes"; then
AC_DEFINE(HAVE_STRUCT_SOCKADDR_IN_SIN_LEN, 1,
            [Whether struct sockaddr_in has the sin_len member])
fi

if test "x${ac_cv_member_struct_sockaddr_in6_sin6_len}" = "xyes"; then
    AC_DEFINE(HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN, 1,
            [Whether struct sockaddr_in6 has the sin6_len member])
fi

if test "x${ac_cv_member_struct_tcp_info_tcpi_data_segs_out}" = "xyes"; then
    AC_DEFINE(HAVE_STRUCT_LINUX_TCP_INFO, 1,
            [Whether struct tcp_info have the tcpi_data_segs_{in,out} member])
fi

if test "x${with_profiler}" = "xyes"; then
AC_CHECK_HEADERS([gperftools/profiler.h \
                  ], [], [])
fi

if test "x${enable_posix_cap}" != "xno"; then
  AC_CHECK_HEADERS([sys/capability.h],
    [],
    [AC_MSG_FAILURE([Found POSIX capabilities library but not the header sys/capability.h. POSIX capabilities are not a required feature, you can disable then with --disable-posix-cap])],
    []
  )
fi

# Check for high-resolution timestamps in struct stat
AC_CHECK_MEMBERS([struct stat.st_mtimespec.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_mtim.tv_nsec])


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

TS_CHECK_SOCKOPT(SO_PEERCRED, [has_so_peercred=1], [has_so_peercred=0])
TS_CHECK_SOCKOPT(SO_MARK, [has_so_mark=1], [has_so_mark=0])
TS_CHECK_SOCKOPT(IP_TOS, [has_ip_tos=1], [has_ip_tos=0])

AC_SUBST(has_so_mark)
AC_SUBST(has_ip_tos)
AC_SUBST(has_so_peercred)

TS_CHECK_MACRO_IN6_IS_ADDR_UNSPECIFIED

AC_CHECK_TYPE([struct tcp_info],
  [AC_DEFINE(HAVE_STRUCT_TCP_INFO, 1, [whether struct tcp_info is available])],
  [],
  [[
   #include <netinet/in.h>
   #include <netinet/tcp.h>
  ]]
)

AC_MSG_CHECKING([whether to include systemtap tracing support])
AC_ARG_ENABLE([systemtap],
              [AS_HELP_STRING([--enable-systemtap],
                              [Enable inclusion of systemtap trace support])],
              [ENABLE_SYSTEMTAP="${enableval}"], [ENABLE_SYSTEMTAP='no'])
AM_CONDITIONAL([ENABLE_SYSTEMTAP], [test x$ENABLE_SYSTEMTAP = xyes])
AC_MSG_RESULT(${ENABLE_SYSTEMTAP})

if test "x${ENABLE_SYSTEMTAP}" = xyes; then
  AC_CHECK_PROGS(DTRACE, dtrace)
  if test -z "$DTRACE"; then
    AC_MSG_ERROR([dtrace not found])
  fi
  AC_CHECK_HEADER([sys/sdt.h], [SDT_H_FOUND='yes'],
                  [SDT_H_FOUND='no';
                     AC_MSG_ERROR([systemtap support needs sys/sdt.h header])])
  AC_DEFINE([HAVE_SYSTEMTAP], [1], [Define to 1 if using probes.])
fi

# See if we can build the remap_stats plugin
AS_IF([test "x$enable_experimental_plugins" = "xyes"],
  [
    AC_CHECK_HEADERS([search.h])
    AS_IF([test "x$ac_cv_header_search_h" = "xyes"],
      [
        AC_CHECK_TYPE([struct hsearch_data],[],[],[[#include <search.h>]])
        AC_CHECK_FUNCS([hcreate_r hsearch_r])
      ])
  ])

AC_ARG_WITH([default-stack-size],
  [AS_HELP_STRING([--with-default-stack-size],[specify the default stack size in bytes [default=1048576]])],
  [
  with_default_stack_size="$withval"
  ],[
  with_default_stack_size="1048576"
  ]
)

AC_SUBST([default_stack_size], [$with_default_stack_size])

#
# use modular IOCORE
#
iocore_include_dirs="\
-I\$(abs_top_srcdir)/iocore/eventsystem \
-I\$(abs_top_srcdir)/iocore/net \
-I\$(abs_top_srcdir)/iocore/net/quic \
-I\$(abs_top_srcdir)/iocore/aio \
-I\$(abs_top_srcdir)/iocore/hostdb \
-I\$(abs_top_srcdir)/iocore/cache \
-I\$(abs_top_srcdir)/iocore/utils \
-I\$(abs_top_srcdir)/iocore/dns"

AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])
AC_SUBST([iocore_include_dirs])

# NOTE: All additions to the default include path must be added to
# TS_INCLUDES *not* to AM_CPPFLAGS. If you add then to AM_CPPFLAGS
# then they are always prepended to the local AM_CPPFLAGS which risks
# name collisions with in-tree files. We always want the in-tree files
# to have precedence.
AC_SUBST([TS_INCLUDES])

AS_IF([test "x$RPATH" != "x"], [
       TS_ADDTO_RPATH([$RPATH])
])

# -----------------------------------------------------------------------------
# 6. OUTPUT FILES

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  configs/Makefile
  configs/body_factory/Makefile
  configs/body_factory/default/Makefile
  configs/records.yaml.default
  configs/storage.config.default
  doc/Makefile
  doc/ext/local-config.py
  doc/uml/Makefile
  example/Makefile
  example/plugins/Makefile
  example/plugins/c-api/Makefile
  example/plugins/cpp-api/Makefile
  include/Makefile
  include/ts/Makefile
  include/tscpp/api/Makefile
  include/tscpp/util/Makefile
  include/ts/apidefs.h
  include/tscore/ink_config.h
  iocore/Makefile
  iocore/aio/Makefile
  iocore/cache/Makefile
  iocore/dns/Makefile
  iocore/eventsystem/Makefile
  iocore/hostdb/Makefile
  iocore/net/Makefile
  iocore/net/quic/Makefile
  iocore/utils/Makefile
  lib/Makefile
  src/tscpp/api/Makefile
  lib/perl/Makefile
  lib/perl/lib/Apache/TS.pm
  src/wccp/Makefile
  lib/fastlz/Makefile
  lib/swoc/Makefile
  lib/yamlcpp/Makefile
  mgmt/Makefile
  mgmt/api/Makefile
  mgmt/api/include/Makefile
  mgmt/utils/Makefile
  mgmt/rpc/Makefile
  mgmt/config/Makefile
  plugins/Makefile
  proxy/Makefile
  proxy/hdrs/Makefile
  proxy/http/Makefile
  proxy/http/remap/Makefile
  proxy/http2/Makefile
  proxy/http3/Makefile
  proxy/logging/Makefile
  proxy/shared/Makefile
  rc/Makefile
  rc/trafficserver
  rc/trafficserver.conf
  rc/trafficserver.service
  rc/trafficserver.xml
  src/tscpp/util/Makefile
  src/tscore/Makefile
  src/records/Makefile
  tools/Makefile
  tools/trafficserver.pc
  tools/tsxs
  tests/Makefile
])

# -----------------------------------------------------------------------------
# 7. autoheader TEMPLATES

AC_OUTPUT

AC_MSG_NOTICE([Build option summary:
    CC:                 $CC
    CXX:                $CXX
    CPP:                $CPP
    CFLAGS:             $CFLAGS
    CXXFLAGS:           $CXXFLAGS
    CPPFLAGS:           $CPPFLAGS
    LDFLAGS:            $LDFLAGS
    AM@&t@_CFLAGS:      $AM_CFLAGS
    AM@&t@_CXXFLAGS:    $AM_CXXFLAGS
    AM@&t@_CPPFLAGS:    $AM_CPPFLAGS
    AM@&t@_LDFLAGS:     $AM_LDFLAGS
    TS_INCLUDES:        $TS_INCLUDES
    OPENSSL_LDFLAGS:    $OPENSSL_LDFLAGS
    OPENSSL_INCLUDES:   $OPENSSL_INCLUDES
    YAMLCPP_LDFLAGS:    $YAMLCPP_LDFLAGS
    YAMLCPP_INCLUDES:   $YAMLCPP_INCLUDES
    SWOC_LDFLAGS:       $SWOC_LDFLAGS
    SWOC_INCLUDES:      $SWOC_INCLUDES
    NURAFT_LDFLAGS:     $NURAFT_LDFLAGS
    NURAFT_INCLUDES:    $NURAFT_INCLUDES
])
