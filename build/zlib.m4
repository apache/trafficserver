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
dnl zlib.m4: Trafficserver's zlib autoconf macros
dnl

dnl
dnl TS_CHECK_ZLIB: look for zlib libraries and headers
dnl
AC_DEFUN([TS_CHECK_ZLIB], [
enable_zlib=no
AC_ARG_WITH(zlib, [AC_HELP_STRING([--with-zlib=DIR],[use a specific zlib library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    zlib_base_dir="$withval"
    if test "$withval" != "no"; then
      enable_zlib=yes
      case "$withval" in
      *":"*)
        zlib_include="`echo $withval |sed -e 's/:.*$//'`"
        zlib_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for zlib includes in $zlib_include libs in $zlib_ldflags )
        ;;
      *)
        zlib_include="$withval/include"
        zlib_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for zlib includes in $withval)
        ;;
      esac
    fi
  fi
])

if test "x$zlib_base_dir" = "x"; then
  AC_MSG_CHECKING([for zlib location])
  AC_CACHE_VAL(ats_cv_zlib_dir,[
  for dir in /usr/local /usr ; do
    if test -d $dir && test -f $dir/include/zlib.h; then
      ats_cv_zlib_dir=$dir
      break
    fi
  done
  ])
  zlib_base_dir=$ats_cv_zlib_dir
  if test "x$zlib_base_dir" = "x"; then
    enable_zlib=no
    AC_MSG_RESULT([not found])
  else
    enable_zlib=yes
    zlib_include="$zlib_base_dir/include"
    zlib_ldflags="$zlib_base_dir/lib"
    AC_MSG_RESULT([$zlib_base_dir])
  fi
else
  if test -d $zlib_include && test -d $zlib_ldflags && test -f $zlib_include/zlib.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
fi

zlibh=0
if test "$enable_zlib" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  zlib_have_headers=0
  zlib_have_libs=0
  if test "$zlib_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${zlib_include}])
    TS_ADDTO(LDFLAGS, [-L${zlib_ldflags}])
    TS_ADDTO_RPATH(${zlib_ldflags})
  fi
  AC_SEARCH_LIBS([compressBound], [z], [zlib_have_libs=1])
  if test "$zlib_have_libs" != "0"; then
    AC_CHECK_HEADERS(zlib.h, [zlib_have_headers=1])
  fi
  if test "$zlib_have_headers" != "0"; then
    AC_SUBST(LIBZ, [-lz])
  else
    enable_zlib=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
AC_SUBST(zlibh)
])
