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
dnl jansson.m4: Trafficserver's jansson autoconf macros
dnl

dnl
dnl TS_CHECK_JANSSON: look for jansson libraries and headers
dnl

AC_DEFUN([TS_CHECK_JANSSON], [
  AC_MSG_CHECKING([for --with-jansson])
  AC_ARG_WITH(
      [jansson],
      [AS_HELP_STRING([--with-jansson], [use a specific jansson library])],
      [ LDFLAGS="$LDFLAGS -L$with_jansson/lib";
        CFLAGS="$CFLAGS -I$with_jansson/include/";
        CPPFLAGS="$CPPFLAGS -I$with_jansson/include/";
        AC_MSG_RESULT([$with_jansson])
      ],
      [ AC_MSG_RESULT([no])]
  )

  AC_CHECK_HEADERS([jansson.h], [
    AC_MSG_CHECKING([whether jansson is dynamic])
    TS_LINK_WITH_FLAGS_IFELSE([-fPIC -ljansson],[AC_LANG_PROGRAM(
                              [#include <jansson.h>],
                              [(void) json_object();])],
                              [AC_MSG_RESULT([yes]); LIBJANSSON=-ljansson],
                              [AC_MSG_RESULT([no]);  LIBJANSSON=-l:libjansson.a])
    ],
    [LIBJANSSON=])
])
