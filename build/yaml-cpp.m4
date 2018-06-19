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
dnl yaml-cpp.m4: Trafficserver's yaml-cpp autoconf macros
dnl

dnl
dnl TS_CHECK_YAML_CPP: look for yaml-cpp libraries and headers
dnl
AC_DEFUN([TS_CHECK_YAML_CPP], [
has_yaml_cpp=no
AC_ARG_WITH(yaml-cpp, [AC_HELP_STRING([--with-yaml-cpp=DIR],[use a specific yaml-cpp library])],
[
  if test "x$withval" != "xyes" && test "x$withval" != "x"; then
    yaml_cpp_base_dir="$withval"
    if test "$withval" != "no"; then
      has_yaml_cpp=yes
      case "$withval" in
      *":"*)
        yaml_cpp_include="`echo $withval |sed -e 's/:.*$//'`"
        yaml_cpp_ldflags="`echo $withval |sed -e 's/^.*://'`"
        AC_MSG_CHECKING(checking for yaml-cpp includes in $yaml_cpp_include libs in $yaml_cpp_ldflags )
        ;;
      *)
        yaml_cpp_include="$withval/include"
        yaml_cpp_ldflags="$withval/lib"
        yaml_cpp_base_dir="$withval"
        AC_MSG_CHECKING(yaml-cpp includes in $withval libs in $yaml_cpp_ldflags)
        ;;
      esac
    fi
  fi

  if test -d $yaml_cpp_include && test -d $yaml_cpp_ldflags && test -f $yaml_cpp_include/yaml-cpp/yaml.h; then
    AC_MSG_RESULT([ok])
  else
    AC_MSG_RESULT([not found])
  fi

if test "$has_yaml_cpp" != "no"; then
  saved_ldflags=$LDFLAGS
  saved_cppflags=$CPPFLAGS

  YAMLCPP_LIBS=-lyaml-cpp
  if test "$yaml_cpp_base_dir" != "/usr"; then
    YAMLCPP_INCLUDES=-I${yaml_cpp_include}
    YAMLCPP_LDFLAGS=-L${yaml_cpp_ldflags}

    TS_ADDTO_RPATH(${yaml_cpp_ldflags})
  fi

  if test "$yaml_cpp_include" != "0"; then
    YAMLCPP_INCLUDES=-I${yaml_cpp_include}
  else
    has_yaml_cpp=no
    CPPFLAGS=$saved_cppflags
    LDFLAGS=$saved_ldflags
  fi
fi
],
[
  has_yaml_cpp=no
  YAMLCPP_INCLUDES=-I\${abs_top_srcdir}/lib/yamlcpp/include
  YAMLCPP_LIBS=-lyamlcpp
  YAMLCPP_LDFLAGS=-L\${abs_top_builddir}/lib/yamlcpp
])

AC_SUBST([YAMLCPP_INCLUDES])
AC_SUBST([YAMLCPP_LIBS])
AC_SUBST([YAMLCPP_LDFLAGS])

])
