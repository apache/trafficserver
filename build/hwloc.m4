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
dnl hwloc.m4: Trafficserver's hwloc autoconf macros
dnl

dnl
dnl TS_CHECK_HWLOC: look for hwloc libraries and headers
dnl
AC_DEFUN([TS_CHECK_HWLOC], [
  hwloc_path_provided=no
  AC_ARG_WITH(hwloc, [AC_HELP_STRING([--with-hwloc=DIR],[use a specific hwloc library])],
  [
    if test "x$withval" != "xyes" && test "x$withval" != "x"; then
      hwloc_base_dir="$withval"
      if test "$withval" != "no"; then
        hwloc_path_provided=yes
        case "$withval" in
        *":"*)
          hwloc_include="`echo $withval |sed -e 's/:.*$//'`"
          hwloc_ldflags="`echo $withval |sed -e 's/^.*://'`"
          AC_MSG_CHECKING(checking for hwloc includes in $hwloc_include libs in $hwloc_ldflags )
          ;;
        *)
          hwloc_include="$withval/include"
          hwloc_ldflags="$withval/lib"
          AC_MSG_CHECKING(checking for hwloc includes in $withval)
          ;;
        esac
      fi
    fi
  ])

  if test "x$hwloc_path_provided" = "xno"; then
    # Use pkg-config, because some distros (*cough* Ubuntu) put hwloc in unusual places.
    PKG_CHECK_MODULES([HWLOC], [hwloc], [], [
      AC_SUBST([HWLOC_CFLAGS],[""])
      AC_SUBST([HWLOC_LIBS],[""])
      AC_MSG_RESULT([no])
      $2
    ])
  else
    if test -d $hwloc_include && test -d $hwloc_ldflags && test -f $hwloc_include/hwloc.h; then
      saved_ldflags=$LDFLAGS
      saved_cppflags=$CPPFLAGS
      hwloc_have_headers=0
      hwloc_have_libs=0
      if test "$hwloc_base_dir" != "/usr"; then
        TS_ADDTO(CPPFLAGS, [-I${hwloc_include}])
        TS_ADDTO(LDFLAGS, [-L${hwloc_ldflags}])
        TS_ADDTO_RPATH(${hwloc_ldflags})
      fi
    fi
  fi

  SAVE_LIBS="$LIBS"
  LIBS="-lhwloc"
  AC_LANG_PUSH([C++])
  AC_CHECK_LIBS([hwloc], [hwloc_topology_init], [
    AC_CHECK_HEADERS(hwloc.h, [
      AC_SUBST([HWLOC_CFLAGS])
      AC_SUBST([HWLOC_LIBS])
      AC_MSG_RESULT([yes])
      $1
      # Old versions of libhwloc don't have HWLOC_OBJ_PU.
      AC_CHECK_DECL(HWLOC_OBJ_PU,
        [AC_DEFINE(HAVE_HWLOC_OBJ_PU, 1, [Whether HWLOC_OBJ_PU is available])], [],
        [#include <hwloc.h>]
      )
    ], [
      CPPFLAGS=$saved_cppflags
      LDFLAGS=$saved_ldflags
      AC_SUBST([HWLOC_CFLAGS],[""])
      AC_SUBST([HWLOC_LIBS],[""])
      AC_MSG_RESULT([no])
      $2
    ])
  ], [
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
    AC_SUBST([HWLOC_CFLAGS],[""])
    AC_SUBST([HWLOC_LIBS],[""])
    AC_MSG_RESULT([no])
    $2
  ],[])
  AC_LANG_POP()
  LIBS="$SAVE_LIBS"
])
