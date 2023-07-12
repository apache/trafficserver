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
dnl quiche.m4: Trafficserver's quiche autoconf macros
dnl

dnl
dnl TS_CHECK_QUICHE: look for quiche libraries and headers
dnl
AC_DEFUN([TS_CHECK_QUICHE], [
has_quiche=0
AC_ARG_WITH(quiche, [AS_HELP_STRING([--with-quiche=DIR],[use a specific quiche library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    quiche_base_dir="$withval"
    if test "$withval" != "no"; then
      has_quiche=1
      case "$withval" in
      *":"*)
        quiche_include="`echo $withval | sed -e 's/:.*$//'`"
        quiche_ldflags="`echo $withval | sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for quiche includes in $quiche_include libs in $quiche_ldflags )
        ;;
      *)
        quiche_include="$withval/include"
        quiche_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for quiche includes in $withval)
        ;;
      esac
    fi
  fi

  if test -d $quiche_include && test -d $quiche_ldflags && test -f $quiche_include/quiche.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_quiche" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  saved_libs=$LIBS
  quiche_have_headers=0
  quiche_have_libs=0
  if test "$quiche_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${quiche_include}])
    TS_ADDTO(LDFLAGS, [-L${quiche_ldflags} -rpath ${quiche_ldflags}])
    TS_ADDTO(LIBS, [-lquiche])
    TS_ADDTO_RPATH(${quiche_ldflags})
  fi

  AC_CHECK_LIB([quiche], quiche_connect, [quiche_have_libs=1], [], [$OPENSSL_LIBS])
  if test "$quiche_have_libs" != "0"; then
    AC_CHECK_HEADERS(quiche.h, [quiche_have_headers=1])
  fi
  if test "$quiche_have_headers" != "0"; then
    AC_SUBST([QUICHE_LIB], [-lquiche])
    AC_SUBST([QUICHE_CFLAGS], [-I${quiche_include}])
  else
    has_quiche=0
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
    LIBS=$saved_libs
  fi
fi
],
[
AC_CHECK_HEADER([quiche.h], [], [has_quiche=0])
AC_CHECK_LIB([quiche], quiche_connect, [:], [has_quiche=0], [$OPENSSL_LIBS])
])
])
