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
dnl cjose.m4: Trafficserver's cjose autoconf macros
dnl

dnl
dnl TS_CHECK_CJOSE: look for cjose libraries and headers
dnl

AC_DEFUN([TS_CHECK_CJOSE], [
AC_MSG_CHECKING([for --with-cjose])
  AC_ARG_WITH(
      [cjose],
      [AS_HELP_STRING([--with-cjose=DIR], [use a specific cjose library])],
      [ LDFLAGS="$LDFLAGS -L$with_cjose/lib";
        CFLAGS="$CFLAGS -I$with_cjose/include/";
        CPPFLAGS="$CPPFLAGS -I$with_cjose/include/";
        AC_MSG_RESULT([$with_cjose])
      ],
      [ AC_MSG_RESULT([no])]
  )

  AC_CHECK_HEADERS([cjose/cjose.h], [
    AC_MSG_CHECKING([whether cjose is dynamic])
    TS_LINK_WITH_FLAGS_IFELSE([-fPIC -lcjose -ljansson -lcrypto],[AC_LANG_PROGRAM(
                              [#include <cjose/cjose.h>],
                              [(void) cjose_jws_import("", 0, NULL);])],
                              [AC_MSG_RESULT([yes]); LIBCJOSE=-lcjose],
                              [AC_MSG_RESULT([no]);  LIBCJOSE=-l:libcjose.a])
    ],
    [LIBCJOSE=])
])
