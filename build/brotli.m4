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
dnl brotli.m4: Trafficserver's brotli autoconf macros
dnl

dnl
dnl TS_CHECK_BROTLI: look for brotli libraries and headers
dnl
AC_DEFUN([TS_CHECK_BROTLI], [
has_brotli=0
AC_ARG_WITH(brotli, [AC_HELP_STRING([--with-brotli=DIR],[use a specific brotli library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    brotli_base_dir="$withval"
    if test "$withval" != "no"; then
      has_brotli=1
      case "$withval" in
      *":"*)
        brotli_include="`echo $withval | sed -e 's/:.*$//'`"
        brotli_ldflags="`echo $withval | sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for brotli includes in $brotli_include libs in $brotli_ldflags )
        ;;
      *)
        brotli_include="$withval/include"
        brotli_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for brotli includes in $withval)
        ;;
      esac
    fi
  fi

  if test -d $brotli_include && test -d $brotli_ldflags && test -f $brotli_include/brotli/encode.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_brotli" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  brotli_have_headers=0
  brotli_have_libs=0
  if test "$brotli_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${brotli_include}])
    TS_ADDTO(LDFLAGS, [-L${brotli_ldflags}])
    TS_ADDTO_RPATH(${brotli_ldflags})
  fi

  AC_CHECK_LIB([brotlienc], BrotliEncoderCreateInstance, [brotli_have_libs=1])
  if test "$brotli_have_libs" != "0"; then
    AC_CHECK_HEADERS(brotli/encode.h, [brotli_have_headers=1])
  fi
  if test "$brotli_have_headers" != "0"; then
    AC_SUBST([BROTLIENC_LIB], [-lbrotlienc])
    AC_SUBST([BROTLIENC_CFLAGS], [-I${brotli_include}])
  else
    has_brotli=0
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
],
[
AC_CHECK_HEADER([brotli/encode.h], [], [has_brotli=0])
AC_CHECK_LIB([brotlienc], BrotliEncoderCreateInstance, [:], [has_brotli=0])

if test "x$has_brotli" == "x0"; then
    PKG_CHECK_EXISTS([libbrotlienc],
    [
      PKG_CHECK_MODULES([LIBBROTLIENC], [libbrotlienc >= 0.6.0], [
        AC_CHECK_HEADERS(brotli/encode.h, [brotli_have_headers=1])
        if test "$brotli_have_headers" != "0"; then
            AC_SUBST([BROTLIENC_LIB], [$LIBBROTLIENC_LIBS])
            AC_SUBST([BROTLIENC_CFLAGS], [$LIBBROTLIENC_CFLAGS])
        fi
      ], [])
    ], [])
else
    AC_SUBST([BROTLIENC_LIB], [-lbrotlienc])
fi
])

])
