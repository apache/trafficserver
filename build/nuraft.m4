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
dnl nuraft.m4: Trafficserver's nuraft autoconf macros
dnl

dnl
dnl TS_CHECK_NURAFT: look for nuraft libraries and headers
dnl

AC_DEFUN([TS_CHECK_NURAFT], [
has_nuraft=no
AC_ARG_WITH(nuraft, [AS_HELP_STRING([--with-nuraft=DIR], [use a specific nuraft library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    nuraft_base_dir="$withval"
    if test "$withval" != "no"; then
      has_nuraft=yes
      case "$withval" in
      *":"*)
        nuraft_include="`echo $withval | sed -e 's/:.*$//'`"
        nuraft_ldflags="`echo $withval | sed -e 's/^.*://'`"
        AC_MSG_CHECKING(for nuraft includes in $nuraft_include libs in $nuraft_ldflags)
        ;;
      *)
        nuraft_include="$withval/include"
        nuraft_ldflags="$withval/lib"
        nuraft_base_dir="$withval"
        AC_MSG_CHECKING(for nuraft includes in $nuraft_include libs in $nuraft_ldflags)
        ;;
      esac
    fi
  fi

  if test -d $nuraft_include && test -d $nuraft_ldflags && test -f $nuraft_include/libnuraft/nuraft.hxx; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_nuraft" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS

  NURAFT_LIBS=-lnuraft
  if test "$nuraft_base_dir" != "/usr"; then
    NURAFT_INCLUDES=-I${nuraft_include}
    NURAFT_LDFLAGS=-L${nuraft_ldflags}

    TS_ADDTO(CPPFLAGS, [${NURAFT_INCLUDES}])
    TS_ADDTO(LDFLAGS, [${NURAFT_LDFLAGS}])
    TS_ADDTO_RPATH(${nuraft_ldflags})
  fi

  if test "$nuraft_include" != "0"; then
    NURAFT_INCLUDES=-I${nuraft_include}
  else
    has_nuraft=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
],
[
  has_nuraft=no
])

AC_SUBST([NURAFT_INCLUDES])
AC_SUBST([NURAFT_LIBS])
AC_SUBST([NURAFT_LDFLAGS])
])
