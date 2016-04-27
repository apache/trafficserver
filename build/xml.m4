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
dnl xml.m4 Trafficserver's Xml autoconf macros
dnl

dnl
dnl TS_CHECK_XML: look for xml libraries and headers
dnl
AC_DEFUN([TS_CHECK_XML], [
  _xml_parser=none

  AC_ARG_WITH(xml, [AC_HELP_STRING([--with-xml=(expat|libxml2)],[select XML parser])],
  [
    if test "$withval" = "expat" ; then
      TS_CHECK_XML_EXPAT([_xml_parser=expat])
    elif test "$withval" = "libxml2" ; then
      TS_CHECK_XML_LIBXML2([_xml_parser=libxlm2])
    else
      AC_MSG_ERROR([Unrecognised --with-xml option])
    fi
  ],
  [
    # Default to preferring libxml2 over expat.
    TS_CHECK_XML_LIBXML2([_xml_parser=libxml2],[
      TS_CHECK_XML_EXPAT([_xml_parser=expat])
    ])
  ])

  AC_MSG_CHECKING([for an XML parser])
  AC_MSG_RESULT([$_xml_parser])

  AS_IF([test "$_xml_parser" = "none"], [
    AC_MSG_ERROR([missing XML parser

An XML parser (expat or libxml2) is required. Use the
--with-xml option to select between libxml2 and expat.
Use the --with-libxml2 or --with-expat options to select
a specific installation of each library.
    ])
  ])

  unset _xml_parser
])
dnl

dnl TS_CHECK_XML_LIBXML2(action-if-found, action-if-not-found)
AC_DEFUN([TS_CHECK_XML_LIBXML2], [
  enable_libxml2=no
  libxml2_include=""
  libxml2_ldflags=""
  AC_ARG_WITH(libxml2, [AC_HELP_STRING([--with-libxml2=DIR],[use a specific libxml2 library])],
  [
    if test "x$withval" != "xyes" && test "x$withval" != "x"; then
      if test "$withval" = "yes"; then
        enable_libxml2=yes
        libxml2_include="/usr/include/libxml2"
      elif test "$withval" != "no"; then
        enable_libxml2=yes
        libxml2_include="$withval/include/libxml2"
        libxml2_ldflags="-L$withval/lib"
      fi
    fi
  ])
  if test ${enable_libxml2} = "no"; then
    enable_libxml2=yes
    libxml2_include="/usr/include/libxml2"
  fi
  if test ${enable_libxml2} != "no"; then
    AC_CACHE_CHECK([libxml2], [ts_cv_libxml2], [
      ts_libxml2_CPPFLAGS=$CPPFLAGS
      ts_libxml2_LIBS="$LIBS"
      ts_libxml2_LDFLAGS="$LDFLAGS"
      CPPFLAGS="$CPPFLAGS -I$libxml2_include"
      LDFLAGS="$LDFLAGS $libxml2_ldflags"
      LIBS="$LIBS -lxml2"
      AC_TRY_LINK(
        [#include <libxml/parser.h>],
        [xmlSAXHandler sax; xmlCreatePushParserCtxt(&sax, NULL, NULL, 0, NULL);],
        [ts_cv_libxml2=yes],
        [ts_cv_libxml2=no],
      )
      CPPFLAGS=$ts_libxml2_CPPFLAGS
      LIBS=$ts_libxml2_LIBS
      LDFLAGS=$ts_libxml2_LDFLAGS
    ])
    if test $ts_cv_libxml2 = yes ; then
      AC_DEFINE([HAVE_LIBXML2], 1, [Using libxml2])
      if test -d "$libxml2_include" ; then
        TS_ADDTO(CPPFLAGS, [-I${libxml2_include}])
      fi
      if test -d "$libxml2_ldflags" ; then
        TS_ADDTO(LDFLAGS, [-L${libxml2_ldflags}])
        TS_ADDTO_RPATH(${libxml2_ldflags})
      fi
      TS_ADDTO(LIBS, -lxml2)
      # libxml action-if-found
      $1
    else
      AC_MSG_WARN([failed to find libxml2])
      # libxml action-if-not-found
      $2
    fi
  fi
])

dnl TS_CHECK_XML_EXPAT(action-if-found, action-if-not-found)
AC_DEFUN([TS_CHECK_XML_EXPAT], [
enable_expat=no
AC_ARG_WITH(expat, [AC_HELP_STRING([--with-expat=DIR],[use a specific Expat library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    expat_base_dir="$withval"
    if test "$withval" != "no"; then
      enable_expat=yes
      case "$withval" in
      *":"*)
        expat_include="`echo $withval |sed -e 's/:.*$//'`"
        expat_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for Expat includes in $expat_include libs in $expat_ldflags )
        ;;
      *)
        expat_include="$withval/include"
        expat_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for Expat includes in $withval)
        ;;
      esac
    fi
  fi
])

if test "x$expat_base_dir" = "x"; then
  AC_MSG_CHECKING([for Expat location])
  AC_CACHE_VAL(ats_cv_expat_dir,[
  _expat_dir_list=""
  # On Darwin we used to check the OS XSDK for expat, but this causes us
  # to accidentally link the wrong version of pcre (see TS-4385). Best to
  # just avoid the SDK in most cases.
  for dir in /usr/local /usr; do
    if test -d $dir && test -f $dir/include/expat.h; then
      ats_cv_expat_dir=$dir
      break
    fi
  done

  unset _expat_dir_list
  ])

  expat_base_dir=$ats_cv_expat_dir
  if test "x$expat_base_dir" = "x"; then
    enable_expat=no
    AC_MSG_RESULT([not found])
  else
    enable_expat=yes
    expat_include="$expat_base_dir/include"
    expat_ldflags="$expat_base_dir/lib"
    AC_MSG_RESULT([${expat_base_dir}])
  fi
else
  if test -d $expat_include && test -d $expat_ldflags && test -f $expat_include/expat.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
fi

expath=0
if test "$enable_expat" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  expat_have_headers=0
  expat_have_libs=0
  if test "$expat_base_dir" != "/usr"; then
    TS_ADDTO(CPPFLAGS, [-I${expat_include}])
    TS_ADDTO(LDFLAGS, [-L${expat_ldflags}])
    TS_ADDTO_RPATH(${expat_ldflags})
  fi
  AC_SEARCH_LIBS([XML_SetUserData], [expat], [expat_have_libs=1])
  if test "$expat_have_libs" != "0"; then
      AC_CHECK_HEADERS(expat.h, [expat_have_headers=1])
  fi
  if test "$expat_have_headers" != "0"; then

    AC_SUBST([LIBEXPAT],["-lexpat"])
    AC_DEFINE([HAVE_LIBEXPAT],[1],[Define to 1 if you have Expat library])
    # expat action-if-found
    $1
  else
    enable_expat=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
    # expat action-if-not-found
    $2
  fi
fi
AC_SUBST(expath)
])
