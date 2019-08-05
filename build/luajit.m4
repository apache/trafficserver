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
dnl luajit.m4: Trafficserver's luajit autoconf macros
dnl

dnl
dnl TS_CHECK_LUAJIT: look for luajit libraries and headers
dnl
AC_DEFUN([TS_CHECK_LUAJIT], [
has_luajit=0
AC_ARG_WITH(luajit, [AC_HELP_STRING([--with-luajit=DIR], [use a specific luajit library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    luajit_base_dir="$withval"
    if test "$withval" != "no"; then
      has_luajit=1

      case "$withval" in
      *":"*)
        luajit_include="`echo $withval | sed -e 's/:.*$//'`"
        luajit_ldflags="`echo $withval | sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for luajit includes in $luajit_include libs in $luajit_ldflags)
        ;;
      *)
        # looking for versioned subdir
        for version in 2.0 2.1 ; do
            dir="$withval/include/luajit-$version"
            AC_MSG_CHECKING(checking for luajit in $dir)
            if test -d $dir; then
                AC_MSG_RESULT([ok])
                luajit_include=$dir
                break
            else
                AC_MSG_RESULT([not found])
            fi
        done

        if test "x$luajit_include" = "x"; then
            AC_MSG_ERROR([*** could not find luajit include dir ***])
        fi

        luajit_ldflags="$withval/lib"
        ;;
      esac

    fi
  fi

  if test -d $luajit_include && test -d $luajit_ldflags && test -f $luajit_include/luajit.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_luajit" != "0"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  luajit_have_headers=0
  luajit_have_libs=0

  TS_ADDTO(CPPFLAGS, [-I${luajit_include}])
  if test "$luajit_base_dir" != "/usr"; then
    TS_ADDTO(LDFLAGS, [-L${luajit_ldflags}])
    TS_ADDTO_RPATH(${luajit_ldflags})
  fi

  AC_CHECK_LIB([luajit-5.1], luaopen_jit, [luajit_have_libs=1])
  if test "$luajit_have_libs" == "1"; then
    AC_CHECK_HEADERS(luajit.h, [luajit_have_headers=1])
  fi

  if test "$luajit_have_headers" == "1"; then
    AC_SUBST([LUAJIT_LDFLAGS], ["-L${luajit_ldflags} -lluajit-5.1"])
    AC_SUBST([LUAJIT_CPPFLAGS], [-I${luajit_include}])
    enable_luajit=yes
  else
    has_luajit=0
    AC_MSG_ERROR([*** luajit requested but either libluajit-5.1 or luajit.h cannot be found ***])
  fi

  CPPFLAGS=$saved_cppflags
  LDFLAGS=$saved_ldflags
fi
],
[
# use pkg-config to search for it
#

PKG_CHECK_MODULES([LUAJIT], [luajit >= 2.0.4], [
   AC_SUBST([LUAJIT_LDFLAGS], [$LUAJIT_LIBS])
   AC_SUBST([LUAJIT_CPPFLAGS], [$LUAJIT_CFLAGS])
   enable_luajit=yes
],
[
# look in /usr and /usr/local for what we need
#

AC_MSG_CHECKING([for luajit location])
  # looking for versioned subdir
  for version in 2.0 2.1; do
    for lua_prefix in /usr/local /usr; do
      dir="$lua_prefix/include/luajit-$version"

      if test -d $dir; then
        luajit_base_dir=$lua_prefix
        luajit_include=$dir
        luajit_ldflags=$lua_prefix/lib
        break
      fi
    done
  done

  if test "x$luajit_base_dir" = "x"; then
    enable_luajit=no
    AC_MSG_RESULT([not found])
  else
    enable_luajit=yes
    AC_MSG_RESULT([$dir])
  fi

if test "$enable_luajit" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  luajit_have_headers=0
  luajit_have_libs=0

  TS_ADDTO(CPPFLAGS, [-I${luajit_include}])
  if test "$luajit_base_dir" != "/usr"; then
    TS_ADDTO(LDFLAGS, [-L${luajit_ldflags}])
    TS_ADDTO_RPATH(${luajit_ldflags})
  fi

  AC_CHECK_LIB([luajit-5.1], luaopen_jit, [luajit_have_libs=1])
  if test "$luajit_have_libs" == "1"; then
    AC_CHECK_HEADERS(luajit.h, [luajit_have_headers=1])
  fi

  if test "$luajit_have_headers" == "1"; then
    AC_SUBST([LUAJIT_LDFLAGS], ["-L${luajit_ldflags} -lluajit-5.1"])
    AC_SUBST([LUAJIT_CPPFLAGS], [-I${luajit_include}])
    enable_luajit=yes
  else
    has_luajit=0
  fi

  CPPFLAGS=$saved_cppflags
  LDFLAGS=$saved_ldflags
fi

])
])

TS_ARG_ENABLE_VAR([has],[luajit])
AM_CONDITIONAL([HAS_LUAJIT], [test 0 -ne $has_luajit])

dnl On Darwin, LuaJIT requires magic link options for a program loading or running with LuaJIT,
dnl otherwise it will crash in luaL_openlibs() at startup.  See http://luajit.org/install.html for more details
if test "$has_luajit" -ne 0; then
AC_SUBST([LUAJIT_DARWIN_LDFLAGS], ["-Wl,-pagezero_size,10000 -Wl,-image_base,100000000"])
fi
AM_CONDITIONAL([IS_DARWIN], [test x$(uname) = xDarwin])

])
