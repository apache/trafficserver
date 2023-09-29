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
dnl pcre2.m4: Trafficserver's pcre2 autoconf macros
dnl

dnl
dnl TS_CHECK_PCRE2: look for pcre2 libraries and headers
dnl
AC_DEFUN([TS_CHECK_PCRE2], [
has_pcre2=0
AC_ARG_WITH(pcre2, [AS_HELP_STRING([--with-pcre2=DIR],[use a specific pcre2 library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    pcre2_base_dir="$withval"
    if test "$withval" != "no"; then
      has_pcre2=1
      case "$withval" in
      *":"*)
        pcre2_include="`echo $withval | sed -e 's/:.*$//'`"
        pcre2_ldflags="`echo $withval | sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for pcre2 includes in $pcre2_include libs in $pcre2_ldflags )
        ;;
      *)
        pcre2_include="$withval/include"
        pcre2_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for pcre2 includes in $withval)
        ;;
      esac
    fi
  fi

  if test -d $pcre2_include && test -d $pcre2_ldflags && test -f $pcre2_include/pcre2.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_pcre2" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  pcre2_have_headers=0
  if test "$pcre2_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${pcre2_include}])
    TS_ADDTO(LDFLAGS, [-L${pcre2_ldflags}])
    TS_ADDTO_RPATH(${pcre2_ldflags})
  fi

  AC_SUBST([PCRE2_LIB], [-lpcre2-8])
  AC_SUBST([PCRE2_CFLAGS], [-I${pcre2_include}])
fi
],
[
  PKG_CHECK_EXISTS([libpcre2-8],
  [
    PKG_CHECK_MODULES([LIBPCRE2], [libpcre2-8 >= 10.0.0], [
      AC_SUBST([PCRE2_LIB], [$LIBPCRE2_LIBS])
      AC_SUBST([PCRE2_CFLAGS], [$LIBPCRE2_CFLAGS])
    ], [])
  ], [])
])

])
