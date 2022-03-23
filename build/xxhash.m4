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
dnl xxhash.m4: trafficserver's xxhash autoconf macros
dnl

AC_DEFUN([TS_CHECK_XXHASH], [
has_xxhash=no

AC_ARG_WITH([xxhash], [AS_HELP_STRING([--with-xxhash=DIR],[use a specific xxhash library])],
[
  if test "$withval" != "no"; then
    has_xxhash=yes
    xxhash_base_dir="$withval"
    case "$withval" in
      yes)
        xxhash_base_dir="/usr"
        AC_MSG_NOTICE(checking for xxhash includes and libs in standard directories)
        ;;
      *":"*)
        xxhash_include="`echo $withval |sed -e 's/:.*$//'`"
        xxhash_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_NOTICE(checking for xxhash includes in $xxhash_include and libs in $xxhash_ldflags)
        ;;
      *)
        xxhash_include="$withval/include"
        xxhash_ldflags="$withval/lib"
        AC_MSG_NOTICE(checking for xxhash includes in $xxhash_include and libs in $xxhash_ldflags)
        ;;
    esac
  fi
])

has_xxhash=0

if test "$has_xxhash" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS

  XXHASH_LIBS=-lxxhash
  if test "$xxhash_base_dir" != "/usr"; then
     XXHASH_INCLUDES=-I${xxhash_include}
     XXHASH_LDFLAGS=-L${xxhash_ldflags}

    TS_ADDTO_RPATH(${xxhash_ldflags})
  fi

  xxhash_has_libs=0
  AC_CHECK_LIB([xxhash], [XXH64_digest], [xxhash_has_libs=1])

  xxhash_has_headers=0
  if test "xxhash_has_libs" != "0"; then
    AC_CHECK_HEADERS(xxhash.h, [xxhash_has_headers=1])
  fi

  if test "$xxhash_has_headers" != "0"; then
    AC_RUN_IFELSE([
      AC_LANG_PROGRAM(
        [#include <xxhash.h>],
        [
          #ifndef XXH_VERSION_MAJOR
          exit(1);
          #endif
        ]
      )],
      [has_xxhash=1],
      [AC_MSG_ERROR(xxhash has bogus version)]
    )
  else
    AC_MSG_WARN([xxhash not found])
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi

AC_SUBST(has_xxhash)
AC_SUBST(XXHASH_INCLUDES)
AC_SUBST(XXHASH_LDFLAGS)
AC_SUBST(XXHASH_LIBS)
])
