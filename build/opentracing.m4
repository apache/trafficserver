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
dnl opentracing.m4: Trafficserver's opentracing autoconf macros
dnl

AC_DEFUN([TS_CHECK_OPENTRACING], [
enable_opentracing=no
check_opentracing=no
AC_ARG_WITH([opentracing], [AC_HELP_STRING([--with-opentracing=DIR], [use a specific opentracing library])],
[
  if test "$withval" != "no"; then
    check_opentracing=yes
    opentracing_base_dir="$withval"
    case "$withval" in
      yes)
        opentracing_base_dir="/usr"
        ;;
      *":"*)
        opentracing_include_dir="`echo $withval |sed -e 's/:.*$//'`"
        opentracing_lib_dir="`echo $withval |sed -e 's/^.*://'`"
        ;;
      *)
        opentracing_include_dir="$withval/include"
        opentracing_lib_dir="$withval/lib"
        ;;
    esac
  fi
])

if test "$check_opentracing" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  opentracing_have_headers=0
  opentracing_have_libs=0
  opentracing_lib="-lopentracing"
  AC_LANG_PUSH([C++])
  CPPFLAGS="$CPPFLAGS --std=c++17 -I${opentracing_include_dir}"
  LDFLAGS="$LDFLAGS -L ${opentracing_lib_dir} ${opentracing_lib}"
  AC_CHECK_HEADERS(opentracing/tracer.h, [opentracing_have_headers=1])
  if test "$opentracing_have_headers" != "0"; then
    AC_LINK_IFELSE([
        AC_LANG_PROGRAM([
    #include <opentracing/dynamic_load.h>
        ], [
            std::string err_msg;
            opentracing::DynamicallyLoadTracingLibrary("foo", err_msg);
        ])
      ], [
        opentracing_have_libs=1
      ], [])
  fi
  AC_LANG_POP()
  CPPFLAGS=$saved_cppflags
  LDFLAGS=$saved_ldflags

  if test "$opentracing_have_libs" != "0"; then
    AC_SUBST(OPENTRACING_LIBS, ${opentracing_lib})
    enable_opentracing=yes
    if test "$opentracing_base_dir" != "/usr"; then
      TS_ADDTO(CPPFLAGS, [-I${opentracing_include_dir}])
      TS_ADDTO(LDFLAGS, [-L${opentracing_lib_dir}])
      TS_ADDTO_RPATH(${opentracing_lib_dir})
    fi
  fi
fi
])
