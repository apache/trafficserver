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
dnl lzma.m4: Trafficserver's lzma autoconf macros
dnl

dnl
dnl TS_CHECK_LZMA: look for lzma libraries and headers
dnl
AC_DEFUN([TS_CHECK_LZMA], [
enable_lzma=no
AC_ARG_WITH(lzma, [AC_HELP_STRING([--with-lzma=DIR],[use a specific lzma library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    lzma_base_dir="$withval"
    if test "$withval" != "no"; then
      enable_lzma=yes
      case "$withval" in
      *":"*)
        lzma_include="`echo $withval |sed -e 's/:.*$//'`"
        lzma_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for lzma includes in $lzma_include libs in $lzma_ldflags )
        ;;
      *)
        lzma_include="$withval/include"
        lzma_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for lzma includes in $withval)
        ;;
      esac
    fi
  fi
])

if test "x$lzma_base_dir" = "x"; then
  AC_MSG_CHECKING([for lzma location])
  AC_CACHE_VAL(ats_cv_lzma_dir,[
  for dir in /usr/local /usr ; do
    if test -d $dir && test -f $dir/include/lzma.h; then
      ats_cv_lzma_dir=$dir
      break
    fi
  done
  ])
  lzma_base_dir=$ats_cv_lzma_dir
  if test "x$lzma_base_dir" = "x"; then
    enable_lzma=no
    AC_MSG_RESULT([not found])
  else
    enable_lzma=yes
    lzma_include="$lzma_base_dir/include"
    lzma_ldflags="$lzma_base_dir/lib"
    AC_MSG_RESULT([$lzma_base_dir])
  fi
else
  if test -d $lzma_include && test -d $lzma_ldflags && test -f $lzma_include/lzma.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
fi

lzmah=0
if test "$enable_lzma" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  lzma_have_headers=0
  lzma_have_libs=0
  if test "$lzma_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${lzma_include}])
    TS_ADDTO(LDFLAGS, [-L${lzma_ldflags}])
    TS_ADDTO_RPATH(${lzma_ldflags})
  fi
  AC_SEARCH_LIBS([lzma_code], [lzma], [lzma_have_libs=1])
  if test "$lzma_have_libs" != "0"; then
    AC_CHECK_HEADERS(lzma.h, [lzma_have_headers=1])
  fi
  if test "$lzma_have_headers" != "0"; then
    AC_SUBST(LIBLZMA, [-llzma])
  else
    enable_lzma=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
AC_SUBST(lzmah)
])
