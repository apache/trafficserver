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
dnl jemalloc.m4: Trafficserver's jemalloc autoconf macros
dnl

AC_DEFUN([TS_CHECK_JEMALLOC], [
enable_jemalloc=no
AC_ARG_WITH([jemalloc], [AC_HELP_STRING([--with-jemalloc=DIR], [use a specific jemalloc library])],
[
  if test "$withval" != "no"; then
    if test "x${enable_tcmalloc}" = "xyes"; then
      AC_MSG_ERROR([Cannot compile with both jemalloc and tcmalloc])
    fi
    enable_jemalloc=yes
    jemalloc_base_dir="$withval"
    case "$withval" in
      yes)
        jemalloc_base_dir="/usr"
        AC_MSG_CHECKING(checking for jemalloc includes standard directories)
	;;
      *":"*)
        jemalloc_include="`echo $withval |sed -e 's/:.*$//'`"
        jemalloc_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for jemalloc includes in $jemalloc_include libs in $jemalloc_ldflags)
        ;;
      *)
        jemalloc_include="$withval/include"
        jemalloc_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for jemalloc includes in $withval)
        ;;
    esac
  fi
])

jemalloch=0
if test "$enable_jemalloc" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  jemalloc_have_headers=0
  jemalloc_have_libs=0
  if test "$jemalloc_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${jemalloc_include}])
    TS_ADDTO(LDFLAGS, [-L${jemalloc_ldflags}])
    TS_ADDTO_RPATH(${jemalloc_ldflags})
  fi
  # On Darwin, jemalloc symbols are prefixed with je_. Search for that first, then fall back
  # to unadorned symbols.
  AC_SEARCH_LIBS([je_malloc_stats_print], [jemalloc], [jemalloc_have_libs=1],
    [AC_SEARCH_LIBS([malloc_stats_print], [jemalloc], [jemalloc_have_libs=1])]
  )
  if test "$jemalloc_have_libs" != "0"; then
    AC_CHECK_HEADERS(jemalloc/jemalloc.h, [jemalloc_have_headers=1])
  fi
  if test "$jemalloc_have_headers" != "0"; then
    jemalloch=1
    TS_ADDTO(LIBS, [-ljemalloc])
  else
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
AC_SUBST(jemalloch)
])
