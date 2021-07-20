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
dnl TS_CHECK_BORINGOCSP: look for boringocsp libraries and headers
dnl
AC_DEFUN([TS_CHECK_BORINGOCSP], [
has_boringocsp=no
AC_ARG_WITH(boringocsp, [AS_HELP_STRING([--with-boringocsp=DIR], [use a specific BoringOCSP library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    boringocsp_base_dir="$withval"
    if test "$withval" != "no"; then
      has_boringocsp=1
      use_tls_ocsp=1
      case "$withval" in
      *":"*)
        boringocsp_include="`echo $withval |sed -e 's/:.*$//'`"
        boringocsp_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for boringocsp includes in $boringocsp_include libs in $boringocsp_ldflags)
        ;;
      *)
        boringocsp_include="$withval"
        boringocsp_ldflags="$withval"
        boringocsp_base_dir="$withval"
        AC_MSG_CHECKING(boringocsp includes in $withval libs in $boringocsp_ldflags)
        ;;
      esac
    fi
  fi

  if test -d $boringocsp_include && test -d $boringocsp_ldflags && test -f $boringocsp_include/ocsp.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
  AC_SUBST(use_boringocsp)

if test "$has_boringocsp" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS

  BORINGOCSP_LIBS=-lboringocsp
  if test "$boringocsp_base_dir" != "/usr"; then
    BORINGOCSP_INCLUDES=-I${boringocsp_include}
    BORINGOCSP_LDFLAGS=-L${boringocsp_ldflags}

    TS_ADDTO_RPATH(${boringocsp_ldflags})
  fi

  if test "$boringocsp_include" != "0"; then
    BORINGOCSP_INCLUDES=-I${boringocsp_include}
  else
    has_boringocsp=0
    use_tls_ocsp=0
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
],
[
  has_boringocsp=0
  # we might be non-boringssl, don't disable if test has previously passed
  if test "$use_tls_ocsp" != "1"; then
    use_tls_ocsp=0
  fi
])

AC_SUBST(has_boringocsp)
AC_SUBST(use_tls_ocsp)
AC_SUBST([BORINGOCSP_INCLUDES])
AC_SUBST([BORINGOCSP_LIBS])
AC_SUBST([BORINGOCSP_LDFLAGS])

])
