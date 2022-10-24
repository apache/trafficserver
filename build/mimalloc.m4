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
dnl mimalloc.m4: Trafficserver's mimalloc autoconf macros
dnl

AC_DEFUN([TS_CHECK_MIMALLOC], [
has_mimalloc=no
AC_ARG_WITH([mimalloc], [AS_HELP_STRING([--with-mimalloc=DIR],[use a specific mimalloc library])],
[
  if test "$withval" != "no"; then
    if test "x${has_tcmalloc}" = "xyes"; then
      AC_MSG_ERROR([Cannot compile with both mimalloc and tcmalloc])
    fi
    if test "x${has_jemalloc}" = "xyes"; then
      AC_MSG_ERROR([Cannot compile with both mimalloc and jemalloc])
    fi
    has_mimalloc=yes
    mimalloc_base_dir="$withval"
    case "$withval" in
      yes)
        mimalloc_base_dir="/usr"
        AC_MSG_NOTICE(checking for mimalloc includes and libs in standard directories)
        ;;
      *":"*)
        mimalloc_include="`echo $withval | sed -e 's/:.*$//'`"
        mimalloc_ldflags="`echo $withval | sed -e 's/^.*://'`"
        AC_MSG_NOTICE(checking for mimalloc includes in $mimalloc_include and libs in $mimalloc_ldflags)
        ;;
      *)
        mimalloc_include="$withval/include"
        mimalloc_ldflags="$withval/lib"
        mimalloc_ldflags64="$withval/lib64"
        AC_MSG_NOTICE(checking for mimalloc includes in $mimalloc_include and libs in $mimalloc_ldflags or $mimalloc_ldflags64)
        ;;
    esac
  fi
])

mimalloch=0
if test "$has_mimalloc" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  mimalloc_has_headers=0
  mimalloc_has_libs=0
  AC_CHECK_FILE([mimalloc_ldflags], [], [mimalloc_ldflags=$mimalloc_ldflags64])
  if test "$mimalloc_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${mimalloc_include}])
    TS_ADDTO(LDFLAGS, [-L${mimalloc_ldflags}])
    TS_ADDTO(LDFLAGS, [-Wl,--as-needed -L${mimalloc_ldflags} -Wl,-rpath,${mimalloc_ldflags} -Wl,--no-as-needed])
    TS_ADDTO_RPATH(${mimalloc_ldflags})
  fi
  AC_SEARCH_LIBS([mi_malloc], [mimalloc], [mimalloc_has_libs=1])
  if test "$mimalloc_has_libs" != "0"; then
    AC_CHECK_HEADERS(mimalloc.h, [mimalloc_has_headers=1])
  fi
  if test "$mimalloc_has_headers" != "0"; then
    AC_RUN_IFELSE([
      AC_LANG_PROGRAM(
        [#include <mimalloc.h>],
        [
          #ifndef MI_MALLOC_VERSION
          exit(1);
          #endif

          #if (MI_MALLOC_VERSION == 0)
          exit(1);
          #endif
        ]
      )],
      [mimalloch=1],
      [AC_MSG_ERROR(mimalloc has bogus version)]
    )
  else
    AC_MSG_WARN([mimalloc not found])
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
AC_SUBST(mimalloch)
])
