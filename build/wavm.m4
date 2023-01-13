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
dnl wavm.m4: Trafficserver's wavm autoconf macros
dnl

dnl
dnl TS_CHECK_WAVM: look for wavm libraries and headers
dnl
AC_DEFUN([TS_CHECK_WAVM], [
has_wavm=0
AC_ARG_WITH(wavm, [AS_HELP_STRING([--with-wavm=DIR], [use specific WAVM library])],
[
if test "x$withval" != "xyes" && test "x$withval" != "x"; then
  wavm_base_dir="$withval"
  if test "$withval" != "no"; then
    has_wavm=1

    case "$withval" in
    *":"*)
      wavm_include="`echo $withval | sed -e 's/:.*$//'`"
      wavm_ldflags="`echo $withval | sed -e 's/^.*://'`"
      AC_MSG_CHECKING(checking for WAVM includes in $wavm_include libs in $wavm_ldflags)
      ;;
    *)
      dir="$withval/include/WAVM"
      AC_MSG_CHECKING(checking for wavm in $dir)
      if test -d $dir; then
          AC_MSG_RESULT([ok])
          wavm_include=$dir
        else
          AC_MSG_RESULT([not found])
      fi

      if test "x$wavm_include" = "x"; then
          AC_MSG_ERROR([*** could not find wavm include dir ***])
      fi

      wavm_ldflags="$withval/lib"
      ;;
  esac

  fi
fi

if test -d $wavm_include && test -d $wavm_ldflags && test -f $wavm_include/Platform/Defines.h; then
  AC_MSG_RESULT([$wavm_include/WASM/WASM.h found ok])
else
  AC_MSG_RESULT([$wavm_include/WASM/WASM.h not found])
fi

if test "$has_wavm" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  wavm_have_headers=0
  wavm_have_libs=0

  TS_ADDTO(CPPFLAGS, [-I${wavm_include}])
  if test "$wavm_base_dir" != "/usr"; then
    TS_ADDTO(LDFLAGS, [-L${wavm_ldflags}])
    TS_ADDTO_RPATH(${wavm_ldflags})
  fi

  AC_CHECK_LIB([WAVM], wasm_module_new, [wavm_have_libs=1])
  if test "$wavm_have_libs" == "1"; then
    AC_LANG_PUSH([C++])
    AC_CHECK_HEADERS([Platform/Defines.h], [wavm_have_headers=1])
    AC_LANG_POP([C++])
  fi

  if test "$wavm_have_headers" == "1"; then
    AC_SUBST([WAVM_LDFLAGS], ["-L${wavm_ldflags} -lWAVM"])
    AC_SUBST([WAVM_CPPFLAGS], [-I${wavm_include}])
    AC_DEFINE([WAVM_API], [], [Define to enable WAVM API])
    enable_wavm=yes
  else
    has_wavm=0
    AC_MSG_ERROR([*** wavm requested but either libWAVM or WAVM/Platform/Defines.h cannot be found ***])
  fi

  CPPFLAGS=$saved_cppflags
  LDFLAGS=$saved_ldflags
fi
],
[
# add pkg-config search
#

PKG_CHECK_MODULES([WAVM], [wavm >= 1.0.0], [
   AC_SUBST([WAVM_LDFLAGS], [$WAVM_LIBS])
   AC_SUBST([WAVM_CPPFLAGS], [$WAVM_CFLAGS])
   enable_wavm=yes
],
[
# look in /usr and /usr/local for what we need
#

AC_MSG_CHECKING([for WAVM location])
  for wavm_prefix in /usr/local /usr; do
    dir="$wavm_prefix/include/WAVM"

    if test -d $dir; then
      wavm_base_dir=$wavm_prefix
      wavm_include=$dir
      wavm_ldflags=$wavm_prefix/lib
      break
    fi
  done

  if test "x$wavm_base_dir" = "x"; then
    enable_wavm=no
    AC_MSG_RESULT([$dir not found])
  else
    enable_wavm=yes
    AC_MSG_RESULT([$dir found])
  fi

if test "$enable_wavm" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  wavm_have_headers=0
  wavm_have_libs=0

  TS_ADDTO(CPPFLAGS, [-I${wavm_include}])
  if test "$wavm_base_dir" != "/usr"; then
    TS_ADDTO(LDFLAGS, [-L${wavm_ldflags}])
    TS_ADDTO_RPATH(${wavm_ldflags})
  fi

  AC_CHECK_LIB([WAVM], wasm_module_new, [wavm_have_libs=1])
  if test "$wavm_have_libs" == "1"; then
    AC_LANG_PUSH([C++])
    AC_CHECK_HEADERS([Platform/Defines.h], [wavm_have_headers=1])
    AC_LANG_POP([C++])
  fi

  if test "$wavm_have_headers" == "1"; then
    AC_SUBST([WAVM_LDFLAGS], ["-L${wavm_ldflags} -lWAVM"])
    AC_SUBST([WAVM_CPPFLAGS], [-I${wavm_include}])
    AC_DEFINE([WAVM_API], [], [Define to enable WAVM API])
    enable_wavm=yes
  else
    has_wavm=0
  fi

  CPPFLAGS=$saved_cppflags
  LDFLAGS=$saved_ldflags
fi

])
])

TS_ARG_ENABLE_VAR([has],[wavm])
AM_CONDITIONAL([HAS_WAVM], [test 0 -ne $has_wavm])

])
