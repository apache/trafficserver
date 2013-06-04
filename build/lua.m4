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

dnl Check for Lua 5.1 Libraries
dnl
dnl CHECK_LUA(ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND])
dnl Sets:
dnl  LUA_CFLAGS
dnl  LUA_LIBS
AC_DEFUN([CHECK_LUA],
[dnl

AC_ARG_WITH(
    lua,
    [AC_HELP_STRING([--with-lua=PATH],[Path to the Lua 5.1 prefix])],
    lua_path="$withval",
    :)

dnl # Determine lua lib directory
if test -z "$lua_path"; then
    test_paths=". /usr/local /usr"
else
    test_paths="${lua_path}"
fi

dnl
dnl Note that we check for the existence of lua_getfenv (used to be
dnl luaL_newstate). This is because Lua v5.2 and later deprecates
dnl lua_getfenv() because of changes in how environements are handled.
dnl Also see: https://issues.apache.org/jira/browse/TS-1931
dnl
AC_CHECK_LIB(m, pow, lib_m="-lm")
AC_CHECK_LIB(m, sqrt, lib_m="-lm")
for x in $test_paths ; do
  if test "x$x" = "x."; then
    AC_CHECK_HEADER(lua.h,[
        save_CFLAGS=$CFLAGS
        save_LDFLAGS=$LDFLAGS
        CFLAGS="$CFLAGS"
        LDFLAGS="$LDFLAGS $lib_m"
        AC_CHECK_LIB(lua5.1, lua_getfenv, [
            LUA_LIBS="-llua5.1 $lib_m"
        ],[
            AC_CHECK_LIB(lua-5.1, lua_getfenv, [
                LUA_LIBS="-llua-5.1 $lib_m"
            ],[
                AC_CHECK_LIB(lua, lua_getfenv, [
                    LUA_LIBS="-llua $lib_m"
                ])
            ])
        ])
        LUA_CFLAGS=
        CFLAGS=$save_CFLAGS
        LDFLAGS=$save_LDFLAGS
        break
    ])
  else
    AC_MSG_CHECKING([for lua.h in ${x}/include/lua5.1])
    if test -f ${x}/include/lua5.1/lua.h; then
        AC_MSG_RESULT([yes])
        save_CFLAGS=$CFLAGS
        save_LDFLAGS=$LDFLAGS
        CFLAGS="$CFLAGS"
        LDFLAGS="-L$x/lib $LDFLAGS $lib_m"
        AC_CHECK_LIB(lua5.1, lua_getfenv, [
            LUA_LIBS="-L$x/lib -llua5.1 $lib_m"
            LUA_CFLAGS="-I$x/include/lua5.1"
            ])
        CFLAGS=$save_CFLAGS
        LDFLAGS=$save_LDFLAGS
        break
    else
        AC_MSG_RESULT([no])
    fi
    AC_MSG_CHECKING([for lua.h in ${x}/include/lua51])
    if test -f ${x}/include/lua51/lua.h; then
        AC_MSG_RESULT([yes])
        save_CFLAGS=$CFLAGS
        save_LDFLAGS=$LDFLAGS
        CFLAGS="$CFLAGS"
        LDFLAGS="-L$x/lib/lua51 $LDFLAGS $lib_m"
        AC_CHECK_LIB(lua, lua_getfenv, [
            LUA_LIBS="-L$x/lib/lua51 -llua $lib_m"
            LUA_CFLAGS="-I$x/include/lua51"
            ])
        CFLAGS=$save_CFLAGS
        LDFLAGS=$save_LDFLAGS
        break
    else
        AC_MSG_RESULT([no])
    fi
    AC_MSG_CHECKING([for lua.h in ${x}/include])
    if test -f ${x}/include/lua.h; then
        AC_MSG_RESULT([yes])
        save_CFLAGS=$CFLAGS
        save_LDFLAGS=$LDFLAGS
        CFLAGS="$CFLAGS"
        LDFLAGS="-L$x/lib $LDFLAGS $lib_m"
        AC_CHECK_LIB(lua, lua_getfenv, [
            LUA_LIBS="-L$x/lib -llua $lib_m"
            LUA_CFLAGS="-I$x/include"
            ])
        CFLAGS=$save_CFLAGS
        LDFLAGS=$save_LDFLAGS
        break
    else
        AC_MSG_RESULT([no])
    fi
  fi
done

AC_SUBST(LUA_LIBS)
AC_SUBST(LUA_CFLAGS)

if test -z "${LUA_LIBS}"; then
  AC_MSG_WARN([*** Lua 5.1 library not found.])
  ifelse([$2], ,
    enable_lua="no"
    if test -z "${lua_path}"; then
        AC_MSG_WARN([Lua 5.1 library is required])
    else
        AC_MSG_ERROR([Lua 5.1 library is required])
    fi,
    $2)
else
  ifelse([$1], , , $1) 
fi 
])

