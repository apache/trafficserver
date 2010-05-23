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
dnl dbd.m4: Trafficserver's DBD autoconf macros
dnl

dnl
dnl ATS_CHECK_SQLITE3: look for sqlite3 libraries and headers
dnl
AC_DEFUN([ATS_CHECK_SQLITE3], [
enable_sqlite3=no
AC_ARG_WITH(sqlite3, [AC_HELP_STRING([--with-sqlite3=DIR],[use a specific sqlite3 library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    sqlite3_base_dir="$withval"
    if test "$withval" != "no"; then
      enable_sqlite3=yes
      case "$withval" in
      *":"*)
        sqlite3_include="`echo $withval |sed -e 's/:.*$//'`"
        sqlite3_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for sqlite3 includes in $sqlite3_include libs in $sqlite3_ldflags )
        ;;
      *)
        sqlite3_include="$withval/include"
        sqlite3_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for sqlite3 includes in $withval)
        ;;
      esac
    fi
  fi
])

if test "x$sqlite3_base_dir" = "x"; then
  AC_MSG_CHECKING([for sqlite3 location])
  AC_CACHE_VAL(ats_cv_sqlite3_dir,[
  for dir in /usr/local /usr ; do
    if test -d $dir && test -f $dir/include/sqlite3.h; then
      ats_cv_sqlite3_dir=$dir
      break
    fi
  done
  ])
  sqlite3_base_dir=$ats_cv_sqlite3_dir
  if test "x$sqlite3_base_dir" = "x"; then
    enable_sqlite3=no
    AC_MSG_RESULT([not found])
  else
    enable_sqlite3=yes
    sqlite3_include="$sqlite3_base_dir/include"
    sqlite3_ldflags="$sqlite3_base_dir/lib"
    AC_MSG_RESULT([$sqlite3_base_dir])
  fi
else
  if test -d $sqlite3_include && test -d $sqlite3_ldflags && test -f $sqlite3_include/sqlite3.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
fi

if test "$enable_sqlite3" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  sqlite3_have_headers=0
  sqlite3_have_libs=0
  if test "$sqlite3_base_dir" != "/usr"; then
    ATS_ADDTO(CPPFLAGS, [-I${sqlite3_include}])
    ATS_ADDTO(LDFLAGS, [-L${sqlite3_ldflags}])
    case $host_os in
      solaris*)
        ATS_ADDTO(LDFLAGS, [-R${sqlite3_ldflags}])
        ;;
    esac
  fi
  AC_CHECK_LIB(sqlite3, sqlite3_open_v2, [sqlite3_have_libs=1])
  if test "$sqlite3_have_libs" != "0"; then
    ATS_FLAG_HEADERS(sqlite3.h, [sqlite3_have_headers=1])
  fi
  if test "$sqlite3_have_headers" != "0"; then
    AC_DEFINE(HAVE_SQLITE3,1,[Compiling with Sqlite3 support])
    AC_SUBST([LIBSQLITE3], ["-lsqlite3"])
  else
    enable_sqlite3=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
])

dnl
dnl ATS_CHECK_BDB: look for Berkeley-DB libraries and headers
dnl
AC_DEFUN([ATS_CHECK_BDB], [
enable_libdb=no
AC_ARG_WITH(bdb, [AC_HELP_STRING([--with-libdb=DIR],[use a specific Berkeley-DB library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    libdb_base_dir="$withval"
    if test "$withval" != "no"; then
      enable_libdb=yes
      case "$withval" in
      *":"*)
        libdb_include="`echo $withval |sed -e 's/:.*$//'`"
        libdb_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for libdb includes in $libdb_include libs in $libdb_ldflags )
        ;;
      *)
        libdb_include="$withval/include"
        libdb_ldflags="$withval/lib"
        AC_MSG_CHECKING(checking for libdb includes in $withval)
        ;;
      esac
    fi
  fi
])

if test "x$libdb_base_dir" = "x"; then
  AC_MSG_CHECKING([for libdb location])
  AC_CACHE_VAL(ats_cv_libdb_dir,[
  for dir in /usr/local /usr ; do
    if test -d $dir && test -f $dir/include/db.h; then
      ats_cv_libdb_dir=$dir
      break
    fi
  done
  ])
  libdb_base_dir=$ats_cv_libdb_dir
  if test "x$libdb_base_dir" = "x"; then
    enable_libdb=no
    AC_MSG_RESULT([not found])
  else
    enable_libdb=yes
    libdb_include="$libdb_base_dir/include"
    libdb_ldflags="$libdb_base_dir/lib"
    AC_MSG_RESULT([$libdb_base_dir])
  fi
else
  if test -d $libdb_include && test -d $libdb_ldflags && test -f $libdb_include/db.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi
fi

if test "$enable_libdb" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS
  libdb_have_headers=0
  libdb_have_libs=0
  if test "$libdb_base_dir" != "/usr"; then
    ATS_ADDTO(CPPFLAGS, [-I${libdb_include}])
    ATS_ADDTO(LDFLAGS, [-L${libdb_ldflags}])
    case $host_os in
      solaris*)
        ATS_ADDTO(LDFLAGS, [-R${libdb_ldflags}])
        ;;
    esac
  fi
  AC_CHECK_LIB(db, __db_open, [libdb_have_libs=1])
  AC_CHECK_LIB(db, __db185_open, [libdb_have_libs=1])
  if test "$libdb_have_libs" != "0"; then
    ATS_FLAG_HEADERS(db_185.h db.h, [libdb_have_headers=1])
  fi
  if test "$libdb_have_headers" != "0"; then
    AC_SUBST([LIBDB], ["-ldb"])
  else
    enable_libdb=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
])
