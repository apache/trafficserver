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
dnl
AC_DEFUN([TS_CHECK_LIBSWOC], [
has_libswoc=no
AC_ARG_WITH(libswoc, [AS_HELP_STRING([--with-libswoc=DIR],[use a specific libswoc library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    libswoc_base_dir="$withval"
    if test "$withval" != "no"; then
      has_libswoc=yes
      case "$withval" in
      *":"*)
        swoc_include="`echo $withval |sed -e 's/:.*$//'`"
        swoc_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for libswoc includes in $swoc_include libs in $swoc_ldflags )
        ;;
      *)
        swoc_include="$withval/include"
        swoc_ldflags="$withval/lib"
        libswoc_base_dir="$withval"
        AC_MSG_CHECKING(libswoc includes in $withval libs in $swoc_ldflags)
        ;;
      esac
    fi
  fi

  if test -d $swoc_include && test -d $swoc_ldflags && test -f $swoc_include/libswoc/swoc_version.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_libswoc" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS

  SWOC_LIBS=-ltsswoc
  if test "$libswoc_base_dir" != "/usr"; then
    SWOC_INCLUDES=-I${swoc_include}
    SWOC_LDFLAGS=-L${swoc_ldflags}

    TS_ADDTO_RPATH(${swoc_ldflags})
  fi

  if test "$swoc_include" != "0"; then
    SWOC_INCLUDES=-I${swoc_include}
  else
    has_libswoc=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
],
[
  has_libswoc=no
  SWOC_INCLUDES=-I\${abs_top_srcdir}/lib/swoc/include
  SWOC_LIBS=-ltsswoc
  SWOC_LDFLAGS=-L\${abs_top_builddir}/lib/swoc
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
  [],
  [enable_swoc_headers=no]
)
AC_MSG_RESULT([$enable_swoc_headers])
])
