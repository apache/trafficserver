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

dnl -----------------------------------------------------------------
dnl network.m4: Trafficserver's autoconf macros for testing network support
dnl

dnl
dnl Checks the definition of gethostbyname_r and gethostbyaddr_r
dnl which are different for glibc, solaris and assorted other operating
dnl systems
dnl
dnl Note that this test is executed too early to see if we have all of
dnl the headers.
AC_DEFUN([TS_CHECK_GETHOSTBYNAME_R_STYLE], [

dnl Try and compile a glibc2 gethostbyname_r piece of code, and set the
dnl style of the routines to glibc2 on success
AC_CACHE_CHECK([style of gethostbyname_r routine], ac_cv_gethostbyname_r_style,
TS_TRY_COMPILE_NO_WARNING([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
],[
int tmp = gethostbyname_r((const char *) 0, (struct hostent *) 0,
                          (char *) 0, 0, (struct hostent **) 0, &tmp);
/* use tmp to suppress the warning */
tmp=0;
], ac_cv_gethostbyname_r_style=glibc2, ac_cv_gethostbyname_r_style=none))

if test "$ac_cv_gethostbyname_r_style" = "glibc2"; then
  gethostbyname_r_glibc2=1
  AC_DEFINE(GETHOSTBYNAME_R_GLIBC2, 1, [Define if gethostbyname_r has the glibc style])
else
  gethostbyname_r_glibc2=0
fi

AC_CACHE_CHECK([3rd argument to the gethostbyname_r routines], ac_cv_gethostbyname_r_arg,
TS_TRY_COMPILE_NO_WARNING([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
],[
int tmp = gethostbyname_r((const char *) 0, (struct hostent *) 0,
                          (struct hostent_data *) 0);
/* use tmp to suppress the warning */
tmp=0;
], ac_cv_gethostbyname_r_arg=hostent_data, ac_cv_gethostbyname_r_arg=char))

AC_SUBST(gethostbyname_r_glibc2)
])

dnl
dnl TS_CHECK_LOOPBACK_IFACE: try to figure out default loopback interface
dnl
AC_DEFUN([TS_CHECK_LOOPBACK_IFACE], [
default_loopback_iface=""
AC_MSG_CHECKING([for loopback network interface])
case $host_os in
  linux*)
    default_loopback_iface=lo
  ;;
darwin* | freebsd* | solaris*)
  default_loopback_iface=lo0
  ;;
esac
AC_MSG_RESULT([$default_loopback_iface])
AC_SUBST([default_loopback_iface])
])

dnl
dnl Check on IN6_IS_ADDR_UNSPECIFIED. We can't just check if it is defined
dnl because some releases of FreeBSD and Solaris define it incorrectly.
dnl
AC_DEFUN([TS_CHECK_MACRO_IN6_IS_ADDR_UNSPECIFIED], [
AC_CACHE_CHECK([IN6_IS_ADDR_UNSPECIFIED macro works], ac_cv_macro_in6_is_addr_unspecified,
TS_TRY_COMPILE_NO_WARNING([
#include <netinet/in.h>
],[
  (void) IN6_IS_ADDR_UNSPECIFIED(0);
], ac_cv_macro_in6_is_addr_unspecified=yes, ac_cv_macro_in6_is_addr_unspecified=no))

has_in6_is_addr_unspecified=1
if test "x$ac_cv_macro_in6_is_addr_unspecified" = "xno"; then
  has_in6_is_addr_unspecified=0
fi
AC_SUBST(has_in6_is_addr_unspecified)
])
