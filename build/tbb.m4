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
dnl tbb.m4: Trafficserver's tbb autoconf macros
dnl

dnl
dnl TS_CHECK_TBB: look for tbb libraries and headers
dnl
AC_DEFUN([TS_CHECK_TBB], [
enable_tbb=no
AC_ARG_WITH(tbb, [AC_HELP_STRING([--with-tbb=DIR],[use a specific tbb library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    tbb_base_dir="$withval"
    if test "$withval" != "no"; then
      enable_tbb=yes
      case "$withval" in
      *":"*)
        tbb_include="`echo $withval |sed -e 's/:.*$//'`"
        tbb_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for tbb includes in $tbb_include libs in $tbb_ldflags )
        ;;
      *)
        tbb_include="$withval/include"
        tbb_ldflags="$withval/lib64"
        AC_MSG_CHECKING(checking for tbb includes in $withval)
        ;;
      esac
    fi
  fi
])

if test "x$tbb_base_dir" = "x"; then
  AC_MSG_CHECKING([for tbb location])
  AC_CACHE_VAL(ats_cv_tbb_dir,[
  for dir in /usr/local /usr ; do
    if test -d $dir && ( test -f $dir/include/tbb.h || test -f $dir/include/tbb/tbb.h ); then
      ats_cv_tbb_dir=$dir
      break
    fi
  done
  ])
  tbb_base_dir=$ats_cv_tbb_dir
  if test "x$tbb_base_dir" = "x"; then
    enable_tbb=no
    AC_MSG_RESULT([not found])
  else
    enable_tbb=yes
    tbb_include="$tbb_base_dir/include"
    tbb_ldflags="$tbb_base_dir/lib64"
    AC_MSG_RESULT([$tbb_base_dir])
  fi
else
  AC_MSG_CHECKING(for tbb headers in $tbb_include)
  if test -d $tbb_include && test -d $tbb_ldflags && ( test -f $tbb_include/tbb.h || test -f $tbb_include/tbb/tbb.h ); then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
fi

if test "$enable_tbb" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  tbb_have_headers=1
  tbb_have_libs=1
  TS_ADDTO(CPPFLAGS, [-I${tbb_include}])
  TS_ADDTO(LDFLAGS, [-L${tbb_ldflags}])
  TS_ADDTO_RPATH(${tbb_ldflags})
  AC_DEFINE(HAVE_LIBTBB,1,[Compiling with tbb support])
  AC_SUBST(LIBTBB, [-ltbb])
fi
])
