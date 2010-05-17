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
dnl pcre.m4: Trafficserver's pcre autoconf macros
dnl

dnl
dnl ATS_CHECK_PCRE: look for pcre libraries and headers
dnl
AC_DEFUN([ATS_CHECK_PCRE], [
enable_pcre=no
AC_ARG_WITH(pcre, [AC_HELP_STRING([--with-pcre=DIR],[use a specific pcre library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    pcre_base_dir="$withval"
    if test "$withval" != "no"; then
      enable_pcre=yes
      case "$withval" in
      *":"*)
        pcre_include="`echo $withval |sed -e 's/:.*$//'`"
        pcre_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for pcre includes in $pcre_include libs in $pcre_ldflags )
        ;;
      *)
        pcre_include="$withval/include"
        pcre_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for pcre includes in $withval)
        ;;
      esac
    fi
  fi
])

if test "x$pcre_base_dir" = "x"; then
  AC_MSG_CHECKING([for pcre location])
  AC_CACHE_VAL(cv_pcre_dir,[
  for dir in /usr/local /usr ; do
    if test -d $dir && ( test -f $dir/include/pcre.h || test -f $dir/include/pcre/pcre.h ); then
      cv_pcre_dir=$dir
      break
    fi
  done
  ])
  pcre_base_dir=$cv_pcre_dir
  if test "x$pcre_base_dir" = "x"; then
    enable_pcre=no
    AC_MSG_RESULT([not found])
  else
    enable_pcre=yes
    pcre_include="$pcre_base_dir/include"
    pcre_ldflags="$pcre_base_dir/lib"
    AC_MSG_RESULT([$pcre_base_dir])
  fi
else
  if test -d $pcre_include && test -d $pcre_ldflags && ( test -f $pcre_include/pcre.h || test -f $pcre_include/pcre/pcre.h ); then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
fi

if test "$enable_pcre" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  pcre_have_headers=0
  pcre_have_libs=0
  if test "$pcre_base_dir" != "/usr"; then
    ATS_ADDTO(CPPFLAGS, [-I${pcre_include}])
    ATS_ADDTO(LDFLAGS, [-L${pcre_ldflags}])
    case $host_os in
      solaris*)
        ATS_ADDTO(LDFLAGS, [-R${pcre_ldflags}])
        ;;
    esac
  fi
  AC_CHECK_LIB(pcre, pcre_exec, [pcre_have_libs=1])
  if test "$pcre_have_libs" != "0"; then
    AC_CHECK_HEADERS(pcre.h, [pcre_have_headers=1])
    AC_CHECK_HEADERS(pcre/pcre.h, [pcre_have_headers=1])
  fi
  if test "$pcre_have_headers" != "0"; then
    AC_DEFINE(HAVE_LIBPCRE,1,[Compiling with pcre support])
    AC_SUBST(LIBPCRE, [-lpcre])
  else
    enable_pcre=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
])
