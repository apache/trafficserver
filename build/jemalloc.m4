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
has_jemalloc=0
AC_ARG_WITH([jemalloc], [AC_HELP_STRING([--with-jemalloc=DIR], [use a specific jemalloc library])])
AS_IF([test -n "$with_jemalloc" -a "x$with_jemalloc" != "xno" -a "x$enable_tcmalloc" = "xyes"],
 [ AC_MSG_ERROR([Cannot compile with both jemalloc and tcmalloc]) ])

AS_IF([test -n "$with_jemalloc" -a "x$with_jemalloc" != "xno" ],[
  case "$withval" in
    (yes)
      PKG_CHECK_MODULES([JEMALLOC], [jemalloc >= 1.0], [have_jemalloc=yes], [:])
      jemalloc_libdir="$($PKG_CONFIG jemalloc --variable=libdir)"
      ;;
    (*":"*)
      jemalloc_incdir="$(echo $withval |sed -e 's/:.*$//')"
      jemalloc_libdir="$(echo $withval |sed -e 's/^.*://')"
      ;;
    (*)
      jemalloc_incdir="$withval/include"
      jemalloc_libdir="$withval/lib"
      ;;
  esac

  test -z "$jemalloc_libdir" -o -d "$jemalloc_libdir" || jemalloc_libdir=
  test -z "$jemalloc_incdir" -o -d "$jemalloc_incdir" || jemalloc_incdir=

  jemalloc_incdir="${jemalloc_incdir:+$(cd $jemalloc_incdir; cd jemalloc 2>/dev/null; pwd -P)}"
  jemalloc_libdir="${jemalloc_libdir:+$(cd $jemalloc_libdir; pwd -P)}"

  : ${JEMALLOC_CFLAGS:="${jemalloc_incdir:+ -I$jemalloc_incdir}"}
  : ${JEMALLOC_LDFLAGS:="${jemalloc_libdir:+ -L$jemalloc_libdir}"}

  save_cppflags="$CPPFLAGS"
  save_ldflags="$LDFLAGS"
  LDFLAGS="$LDFLAGS $JEMALLOC_LDFLAGS"
  CPPFLAGS="$CPPFLAGS $JEMALLOC_CFLAGS"

dnl
dnl define HAVE_LIBJEMALLOC is to be set and -ljemalloc added into LIBS?
dnl
  AC_CHECK_LIB([jemalloc],[je_malloc_stats_print],[],
    [AC_CHECK_LIB([jemalloc],[malloc_stats_print],[],
    [have_jemalloc=no])])

  AC_CHECK_HEADERS(jemalloc.h, [], [have_jemalloc=no])

  LDFLAGS="$save_ldflags"
  CPPFLAGS="$save_cppflags"

  R=$(expr "$AM_LDFLAGS" : '.*as-needed.*')

  if [ x$R != x0 ]; then
    JEMALLOC_LIBS=' -Wl,--no-as-needed -Wl,-ljemalloc -Wl,--as-needed'
  else
    JEMALLOC_LIBS=' -Wl,-ljemalloc'
  fi

  AS_IF([test "x$have_jemalloc" == "xno" ],
     [AC_MSG_ERROR([Failed to compile with jemalloc.h and -ljemalloc]) ])

  LIBS="$(echo "$LIBS" | sed -e 's/ -ljemalloc//' -e "s/^/$JEMALLOC_LIBS /" )"

  has_jemalloc=1
dnl
dnl flags must be global setting for all libs
dnl
  TS_ADDTO(CPPFLAGS,$JEMALLOC_CFLAGS)
  if test -n "$jemalloc_libdir"; then
      TS_ADDTO_RPATH($jemalloc_libdir)
      TS_ADDTO(LDFLAGS,$JEMALLOC_LDFLAGS)
  fi

  AC_SUBST(JEMALLOC_LIBS)
])

AC_SUBST([has_jemalloc])
])
