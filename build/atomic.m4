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
dnl atomic.m4: Trafficserver's autoconf macros for testing atomic support
dnl

dnl
dnl TS_CHECK_ATOMIC: try to figure out the need for -latomic
dnl
AC_DEFUN([TS_CHECK_ATOMIC], [
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[#include <stdint.h>]],
                     [[uint64_t val = 0; __atomic_add_fetch(&val, 1, __ATOMIC_RELAXED);]])],	
    [AC_DEFINE(HAVE_ATOMIC, 1, [Define to 1 if you have '__atomic' functions.])
      AC_LINK_IFELSE(
        [AC_LANG_PROGRAM([[#include <stdint.h>]],
                         [[uint64_t val = 0; __atomic_add_fetch(&val, 1, __ATOMIC_RELAXED);]])],
        [ATOMIC_LIBS=""],
        [ATOMIC_LIBS="-latomic"]
    )],
    [ATOMIC_LIBS=""]
  )
  AC_SUBST([ATOMIC_LIBS])
])
