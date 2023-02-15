dnl -------------------------------------------------------- -*- autoconf -*-
dnl Licensed to the Apache Software Foundation (ASF) under one or more
dnl contributor license agreements.  See the NOTICE file distributed with
dnl this work for additional information regarding copyright ownership.
dnl The ASF licenses this file to You under the Apache License, Version 2.0
dnl (the "License"); you may not use this file except in compliance with
dnl the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl Unless required by applicable law or agreed to in writing, software
dnl distributed under the License is distributed on an "AS IS" BASIS,
dnl WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
dnl See the License for the specific language governing permissions and
dnl limitations under the License.

dnl
dnl libswoc.m4: Trafficserver's libswoc autoconf macros
dnl

dnl
dnl TS_CHECK_LIBSWOC: look for libswoc libraries and headers
dnl value
dnl   "yes" libswoc is presumed available already and nothing needs to be done.
dnl   "no" Not allowed, generates an error.
dnl   * Presumed to be a directory containing libswoc.
dnl
AC_DEFUN([TS_CHECK_LIBSWOC], [
  # internal defaults.
  has_libswoc=no
  SWOC_INCLUDES=-I\${abs_top_srcdir}/lib/swoc/include
  SWOC_LIBS=-ltsswoc
  SWOC_LDFLAGS=-L\${abs_top_builddir}/lib/swoc
  AC_ARG_WITH(libswoc, [AS_HELP_STRING([--with-libswoc=DIR],[use a specific libswoc library])],
    [
    AC_MSG_CHECKING(checking libswoc)
    # Check for override.
    if test "x$withval" != "x"; then
      has_libswoc=yes # inhibit internal build of libswoc
      if test "$withval" = "no" ; then
        AC_MSG_ERROR([libswoc is required internally, it cannot be disabled])
      elif test "$withval" = "yes" ; then # assume libswoc is installed in a standard place
        SWOC_INCLUDES=
        SWOC_LIBS=-lswoc
        SWOC_LDFLAGS=
        AC_MSG_RESULT([ok])
      else
        swoc_pkg_cfg=""
        # Defaults if pkg config not found.
        SWOC_INCLUDES="-I${withval}/include"
        SWOC_LIBS="-lswoc"
        SWOC_LDFLAGS="-L${withval}/lib"
        if test -n "$PKG_CONFIG" ; then # pkg-config binary was found
          for pk in "lib/pkgconfig" "lib" "." ; do
            if PKG_CONFIG_LIBDIR=${withval}/${pk} $PKG_CONFIG --exists libswoc ; then
              swoc_pkg_cfg=" [pkg-config: ${pk}]"
              SWOC_INCLUDES=$(PKG_CONFIG_LIBDIR=${withval}/${pk} $PKG_CONFIG --cflags libswoc)
              SWOC_LIBS=$(PKG_CONFIG_LIBDIR=${withval}/${pk} $PKG_CONFIG --libs-only-l libswoc)
              SWOC_LDFLAGS=$(PKG_CONFIG_LIBDIR=${withval}/${pk} $PKG_CONFIG --libs-only-L libswoc)
              break
            fi
          done
        fi

        # time to see if things work

        swoc_CXXFLAGS="$CXXFLAGS"
        swoc_LIBS="${LIBS}"
        CXXFLAGS="$CXXFLAGS ${SWOC_INCLUDES}"
        LIBS="${SWOC_LDFLAGS} ${SWOC_LIBS}"

        AC_LANG_PUSH(C++)
        AC_LINK_IFELSE(
          [AC_LANG_PROGRAM([#include <swoc/TextView.h>], [swoc::TextView tv{"Evil Dave Rulz"};])],
          [AC_MSG_RESULT([ok${swoc_pkg_cfg}])],
          [
            AC_MSG_RESULT([failed${swoc_pkg_cfg}])
            AC_MSG_ERROR([${withval} does not contain a valid libswoc.])
          ]
        )
        AC_LANG_POP

        CXXFLAGS="${swoc_CXXFLAGS}"
        LIBS="${swoc_LIBS}"

      fi # valid override
    fi # override provided

    ])

  AC_SUBST([SWOC_INCLUDES])
  AC_SUBST([SWOC_LIBS])
  AC_SUBST([SWOC_LDFLAGS])

])

dnl TS_CHECK_SWOC: check if we want to export libswoc headers from trafficserver. default: not exported
AC_DEFUN([TS_CHECK_SWOC_HEADERS_EXPORT], [
AC_MSG_CHECKING([whether to export libswoc headers])
AC_ARG_ENABLE([swoc-headers],
  [AS_HELP_STRING([--enable-swoc-headers],[Export libswoc headers])],
  [
  if test "x$has_libswoc" = "xyes" ; then
    enable_swoc_headers="no - cannot export external headers"
  fi
  ],
  [enable_swoc_headers=no]
)
AC_MSG_RESULT([$enable_swoc_headers])
])
