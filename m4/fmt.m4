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
dnl fmt.m4: Trafficserver's fmt autoconf macros
dnl

dnl
dnl TS_CHECK_FMT: look for fmt libraries and headers
dnl
AC_DEFUN([TS_CHECK_FMT], [
has_fmt=0
AC_ARG_WITH(fmt, [AS_HELP_STRING([--with-fmt=DIR],[use a specific fmt library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    fmt_base_dir="$withval"
    if test "$withval" != "no"; then
      has_fmt=1
      case "$withval" in
      *":"*)
        fmt_include="`echo $withval | sed -e 's/:.*$//'`"
        fmt_ldflags="`echo $withval | sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for fmt includes in $fmt_include libs in $fmt_ldflags )
        ;;
      *)
        fmt_include="$withval/include"
        fmt_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for fmt includes in $withval)
        ;;
      esac
    fi
  fi

  if test -d $fmt_include && test -d $fmt_ldflags && test -f $fmt_include/fmt/core.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_fmt" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  fmt_have_headers=0
  if test "$fmt_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${fmt_include}])
    TS_ADDTO(LDFLAGS, [-L${fmt_ldflags}])
    TS_ADDTO_RPATH(${fmt_ldflags})
  fi

  AC_SUBST([FMT_LIB], [-lfmt])
  AC_SUBST([FMT_CFLAGS], [-I${fmt_include}])
fi
],
[
  PKG_CHECK_EXISTS([fmt],
  [
    PKG_CHECK_MODULES([LIBFMT], [fmt >= 0.8.0], [
      AC_SUBST([FMT_LIB], [$LIBFMT_LIBS])
      AC_SUBST([FMT_CFLAGS], [$LIBFMT_CFLAGS])
    ], [])
  ], [])
])

])
