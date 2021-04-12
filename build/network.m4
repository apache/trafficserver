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
