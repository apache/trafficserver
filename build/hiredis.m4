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
dnl hiredis.m4: Trafficserver's hiredis autoconf macros
dnl

dnl
dnl TS_CHECK_HIREDIS: look for hiredis libraries and headers
dnl

AC_DEFUN([TS_CHECK_HIREDIS], [
hiredis_base_dir='/usr'
has_hiredis=1
AC_ARG_WITH(hiredis, [AC_HELP_STRING([--with-hiredis=DIR],[use a specific hiredis library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    hiredis_base_dir="$withval"
  fi
],[])

case "$hiredis_base_dir" in
*":"*)
  hidredis_include="`echo $hiredis_base_dir |sed -e 's/:.*$//'`"
  hiredis_ldflags="`echo $hiredis_base_dir |sed -e 's/^.*://'`"
  AC_MSG_CHECKING(for hiredis includes in $hiredis_include libs in $hiredis_ldflags )
  ;;
*)
  hiredis_include="$hiredis_base_dir/include"
  hiredis_ldflags="$hiredis_base_dir/lib"
  AC_MSG_CHECKING(for hiredis includes in $hiredis_base_dir)
  ;;
esac

if test -d $hiredis_include && test -d $hiredis_ldflags && test -f $hiredis_include/hiredis/hiredis.h; then
  AC_MSG_RESULT([yes])
else
  has_hiredis=0
  AC_MSG_RESULT([no])
fi

if test "$has_hiredis" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  hiredis_have_headers=0
  hiredis_have_libs=0
  if test "$hiredis_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${hiredis_include}])
    TS_ADDTO(LDFLAGS, [-L${hiredis_ldflags}])
    TS_ADDTO_RPATH(${hiredis_ldflags})
  fi

  AC_CHECK_LIB([hiredis], redisConnect, [hiredis_have_libs=1])
  if test "$hiredis_have_libs" != "0"; then
    AC_CHECK_HEADERS(hiredis/hiredis.h, [hiredis_have_headers=1])
  fi
  if test "$hiredis_have_headers" != "0"; then
    AC_SUBST([LIB_HIREDIS], [-lhiredis])
    AC_SUBST([CFLAGS_HIREDIS], [-I${hiredis_include}])
  else
    has_hiredis=0
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
])
