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
dnl common.m4: Trafficserver's general-purpose autoconf macros
dnl

dnl
dnl TS_CONFIG_NICE(filename)
dnl
dnl Saves a snapshot of the configure command-line for later reuse
dnl
AC_DEFUN([TS_CONFIG_NICE], [
  rm -f $1
  cat >$1<<EOF
#! /bin/sh
#
# Created by configure

EOF
  if test -n "$CC"; then
    echo "CC=\"$CC\"; export CC" >> $1
  fi
  if test -n "$CXX"; then
    echo "CXX=\"$CXX\"; export CXX" >> $1
  fi
  if test -n "$CFLAGS"; then
    echo "CFLAGS=\"$CFLAGS\"; export CFLAGS" >> $1
  fi
  if test -n "$CXXFLAGS"; then
    echo "CXXFLAGS=\"$CXXFLAGS\"; export CXXFLAGS" >> $1
  fi
  if test -n "$CPPFLAGS"; then
    echo "CPPFLAGS=\"$CPPFLAGS\"; export CPPFLAGS" >> $1
  fi
  if test -n "$LDFLAGS"; then
    echo "LDFLAGS=\"$LDFLAGS\"; export LDFLAGS" >> $1
  fi
  if test -n "$LTFLAGS"; then
    echo "LTFLAGS=\"$LTFLAGS\"; export LTFLAGS" >> $1
  fi
  if test -n "$LIBS"; then
    echo "LIBS=\"$LIBS\"; export LIBS" >> $1
  fi
  if test -n "$INCLUDES"; then
    echo "INCLUDES=\"$INCLUDES\"; export INCLUDES" >> $1
  fi
  if test -n "$NOTEST_CFLAGS"; then
    echo "NOTEST_CFLAGS=\"$NOTEST_CFLAGS\"; export NOTEST_CFLAGS" >> $1
  fi
  if test -n "$NOTEST_CXXFLAGS"; then
    echo "NOTEST_CXXFLAGS=\"$NOTEST_CXXFLAGS\"; export NOTEST_CXXFLAGS" >> $1
  fi
  if test -n "$NOTEST_CPPFLAGS"; then
    echo "NOTEST_CPPFLAGS=\"$NOTEST_CPPFLAGS\"; export NOTEST_CPPFLAGS" >> $1
  fi
  if test -n "$NOTEST_LDFLAGS"; then
    echo "NOTEST_LDFLAGS=\"$NOTEST_LDFLAGS\"; export NOTEST_LDFLAGS" >> $1
  fi
  if test -n "$NOTEST_LIBS"; then
    echo "NOTEST_LIBS=\"$NOTEST_LIBS\"; export NOTEST_LIBS" >> $1
  fi

  # Retrieve command-line arguments.
  eval "set x $[0] $ac_configure_args"
  shift

  for arg
  do
    TS_EXPAND_VAR(arg, $arg)
    echo "\"[$]arg\" \\" >> $1
  done
  echo '"[$]@"' >> $1
  chmod +x $1
])dnl

dnl
dnl TS_SETIFNULL(variable, value)
dnl
dnl  Set variable iff it's currently null
dnl
AC_DEFUN([TS_SETIFNULL], [
  if test -z "$$1"; then
    test "x$verbose" = "xyes" && echo "  setting $1 to \"$2\""
    $1="$2"
  fi
])dnl

dnl
dnl TS_SETVAR(variable, value)
dnl
dnl  Set variable no matter what
dnl
AC_DEFUN([TS_SETVAR], [
  test "x$verbose" = "xyes" && echo "  forcing $1 to \"$2\""
  $1="$2"
])dnl

dnl
dnl TS_ADDTO(variable, value)
dnl
dnl  Add value to variable
dnl
AC_DEFUN([TS_ADDTO], [
  if test "x$$1" = "x"; then
    test "x$verbose" = "xyes" && echo "  setting $1 to \"$2\""
    $1="$2"
  else
    ats_addto_bugger="$2"
    for i in $ats_addto_bugger; do
      ats_addto_duplicate="0"
      for j in $$1; do
        if test "x$i" = "x$j"; then
          ats_addto_duplicate="1"
          break
        fi
      done
      if test $ats_addto_duplicate = "0"; then
        test "x$verbose" = "xyes" && echo "  adding \"$i\" to $1"
        $1="$$1 $i"
      fi
    done
  fi
])dnl

dnl
dnl TS_ADDTO_RPATH(path)
dnl
dnl   Adds path to variable with the '-rpath' directive.
dnl
AC_DEFUN([TS_ADDTO_RPATH], [
  AC_MSG_NOTICE([adding $1 to RPATH])
  TS_ADDTO(AM_LDFLAGS, [-R$1])
])dnl

dnl
dnl TS_REMOVEFROM(variable, value)
dnl
dnl Remove a value from a variable
dnl
AC_DEFUN([TS_REMOVEFROM], [
  if test "x$$1" = "x$2"; then
    test "x$verbose" = "xyes" && echo "  nulling $1"
    $1=""
  else
    ats_new_bugger=""
    ats_removed=0
    for i in $$1; do
      if test "x$i" != "x$2"; then
        ats_new_bugger="$ats_new_bugger $i"
      else
        ats_removed=1
      fi
    done
    if test $ats_removed = "1"; then
      test "x$verbose" = "xyes" && echo "  removed \"$2\" from $1"
      $1=$ats_new_bugger
    fi
  fi
]) dnl

dnl
dnl TS_TRY_COMPILE_NO_WARNING(INCLUDES, FUNCTION-BODY,
dnl             [ACTIONS-IF-NO-WARNINGS], [ACTIONS-IF-WARNINGS])
dnl
dnl Tries a compile test with warnings activated so that the result
dnl is false if the code doesn't compile cleanly.  For compilers
dnl where it is not known how to activate a "fail-on-error" mode,
dnl it is undefined which of the sets of actions will be run.
dnl
dnl We actually always try to link the resulting program, since gcc has
dnl a nasty habit of compiling code that cannot subsequently be linked.
dnl
AC_DEFUN([TS_TRY_COMPILE_NO_WARNING],
[ats_save_CFLAGS=$CFLAGS
 CFLAGS="$CFLAGS $CFLAGS_WARN"
 if test "$ac_cv_prog_gcc" = "yes"; then
   CFLAGS="$CFLAGS -Werror"
 fi
 CFLAGS=$(echo $CFLAGS | sed -e 's/^-w$//' -e 's/^-w //' -e 's/ -w$//' -e 's/ -w / /')
 AC_LINK_IFELSE([AC_LANG_PROGRAM([$1], [$2])], [$3], [$4])
 CFLAGS=$ats_save_CFLAGS
])

dnl
dnl TS_LINK_WITH_FLAGS_IFELSE(LIBS, FUNCTION-BODY,
dnl                           [ACTIONS-IF-LINKS], [ACTIONS-IF-LINK-FAILS])
dnl
dnl Tries a link test with the provided flags.
dnl

AC_DEFUN([TS_LINK_WITH_FLAGS_IFELSE],
[ats_save_LIBS=$LIBS
 LIBS="$LIBS $1"
 AC_LINK_IFELSE([$2],[$3],[$4])
 LIBS=$ats_save_LIBS
])



dnl Iteratively interpolate the contents of the second argument
dnl until interpolation offers no new result. Then assign the
dnl final result to $1.
dnl
dnl Example:
dnl
dnl foo=1
dnl bar='${foo}/2'
dnl baz='${bar}/3'
dnl TS_EXPAND_VAR(fraz, $baz)
dnl   $fraz is now "1/2/3"
dnl
AC_DEFUN([TS_EXPAND_VAR], [
ats_last=
ats_cur="$2"
while test "x${ats_cur}" != "x${ats_last}";
do
  ats_last="${ats_cur}"
  ats_cur=`eval "echo ${ats_cur}"`
done
$1="${ats_cur}"
])


dnl
dnl Removes the value of $3 from the string in $2, strips of any leading
dnl slashes, and returns the value in $1.
dnl
dnl Example:
dnl orig_path="${prefix}/bar"
dnl TS_PATH_RELATIVE(final_path, $orig_path, $prefix)
dnl    $final_path now contains "bar"
AC_DEFUN([TS_PATH_RELATIVE], [
ats_stripped=`echo $2 | sed -e "s#^$3##"`
# check if the stripping was successful
if test "x$2" != "x${ats_stripped}"; then
# it was, so strip of any leading slashes
    $1="`echo ${ats_stripped} | sed -e 's#^/*##'`"
else
# it wasn't so return the original
    $1="$2"
fi
])


dnl TS_SUBST(VARIABLE)
dnl Makes VARIABLE available in generated files
dnl (do not use @variable@ in Makefiles, but $(variable))
AC_DEFUN([TS_SUBST], [
  TS_VAR_SUBST="$TS_VAR_SUBST $1"
  AC_SUBST($1)
])

dnl
dnl TS_SUBST_LAYOUT_PATH
dnl Export (via TS_SUBST) the various path-related variables that
dnl trafficserver will use while generating scripts and
dnl the default config file.
AC_DEFUN([TS_SUBST_LAYOUT_PATH], [
  TS_EXPAND_VAR(exp_$1, [$]$1)
  TS_PATH_RELATIVE(rel_$1, [$]exp_$1, ${prefix})
  TS_SUBST(exp_$1)
  TS_SUBST(rel_$1)
  TS_SUBST($1)
])

dnl TS_HELP_STRING(LHS, RHS)
dnl Autoconf 2.50 can not handle substr correctly.  It does have
dnl AC_HELP_STRING, so let's try to call it if we can.
dnl Note: this define must be on one line so that it can be properly returned
dnl as the help string.  When using this macro with a multi-line RHS, ensure
dnl that you surround the macro invocation with []s
AC_DEFUN([TS_HELP_STRING], [ifelse(regexp(AC_ACVERSION, 2\.1), -1, AC_HELP_STRING([$1],[$2]),[  ][$1] substr([                       ],len($1))[$2])])

dnl
dnl TS_LAYOUT(configlayout, layoutname [, extravars])
dnl
AC_DEFUN([TS_LAYOUT], [
  if test ! -f $srcdir/config.layout; then
    echo "** Error: Layout file $srcdir/config.layout not found"
    echo "** Error: Cannot use undefined layout '$LAYOUT'"
    exit 1
  fi
  # Catch layout names including a slash which will otherwise
  # confuse the heck out of the sed script.
  case $2 in
  */*)
    echo "** Error: $2 is not a valid layout name"
    exit 1 ;;
  esac
  pldconf=./config.pld
  changequote({,})
  sed -e "1s/[ 	]*<[lL]ayout[ 	]*$2[ 	]*>[ 	]*//;1t" \
      -e "1,/[ 	]*<[lL]ayout[ 	]*$2[ 	]*>[ 	]*/d" \
      -e '/[ 	]*<\/Layout>[ 	]*/,$d' \
      -e "s/^[ 	]*//g" \
      -e "s/:[ 	]*/=\'/g" \
      -e "s/[ 	]*$/'/g" \
      $1 > $pldconf
  layout_name=$2
  if test ! -s $pldconf; then
    echo "** Error: unable to find layout $layout_name"
    exit 1
  fi
  . $pldconf
  rm $pldconf
  for var in prefix exec_prefix bindir sbindir libexecdir mandir infodir \
             sysconfdir datadir includedir localstatedir runtimedir \
             logdir libdir installbuilddir libsuffix $3; do
    eval "val=\"\$$var\""
    case $val in
      *+)
        val=`echo $val | sed -e 's;\+$;;'`
        eval "$var=\"\$val\""
        autosuffix=yes
        ;;
      *)
        autosuffix=no
        ;;
    esac
    val=`echo $val | sed -e 's:\(.\)/*$:\1:'`
    val=`echo $val | sed -e 's:[\$]\([a-z_]*\):${\1}:g'`
    if test "$autosuffix" = "yes"; then
      if echo $val | grep -i '/trafficserver$' >/dev/null; then
        addtarget=no
      else
        addtarget=yes
      fi
      if test "$addtarget" = "yes"; then
        val="$val/trafficserver"
      fi
    fi
    eval "$var='$val'"
  done
  for var in bindir sbindir libexecdir mandir infodir sysconfdir \
             datadir localstatedir runtimedir logdir libdir $3; do
    eval "val=\"\$$var\""
    case $val in
      *+)
        val=`echo $val | sed -e 's;\+$;;'`
        eval "$var=\"\$val\""
        autosuffix=yes
        ;;
      *)
        autosuffix=no
        ;;
    esac
    org_val=
    exp_val="$val"
    while test "x${exp_val}" != "x${org_val}";
    do
      org_val="${exp_val}"
      exp_val="`eval \"echo ${exp_val}\"`"
    done
    if echo $exp_val | grep -i '/trafficserver$' >/dev/null; then
      addtarget=no
    else
      addtarget=yes
    fi
    if test "$addsuffix" = "yes" -a "$addtarget" = "yes"; then
      val="$val/trafficserver"
    fi
    var="pkg${var}"
    eval "$var='$val'"
  done
  changequote([,])
])dnl

dnl
dnl TS_ENABLE_LAYOUT(default layout name [, extra vars])
dnl
AC_DEFUN([TS_ENABLE_LAYOUT], [
AC_ARG_ENABLE(layout,
  [TS_HELP_STRING([--enable-layout=LAYOUT],[Enable LAYOUT specified inside config.layout file (defaults to TrafficServer)])],[
  LAYOUT=$enableval
])

if test -z "$LAYOUT"; then
  LAYOUT="$1"
fi
TS_LAYOUT($srcdir/config.layout, $LAYOUT, $2)

AC_MSG_CHECKING(for chosen layout)
AC_MSG_RESULT($layout_name)
])


dnl
dnl TS_PARSE_ARGUMENTS
dnl a reimplementation of autoconf's argument parser,
dnl used here to allow us to co-exist layouts and argument based
dnl set ups.
AC_DEFUN([TS_PARSE_ARGUMENTS], [
ac_prev=
# Retrieve the command-line arguments.  The eval is needed because
# the arguments are quoted to preserve accuracy.
eval "set x $ac_configure_args"
shift
for ac_option
do
# If the previous option needs an argument, assign it.
  if test -n "$ac_prev"; then
    eval "$ac_prev=\$ac_option"
    ac_prev=
    continue
  fi

  ac_optarg=`expr "x$ac_option" : 'x[[^=]]*=\(.*\)'`

  case $ac_option in

  -bindir | --bindir | --bindi | --bind | --bin | --bi)
    ac_prev=bindir ;;
  -bindir=* | --bindir=* | --bindi=* | --bind=* | --bin=* | --bi=*)
    bindir="$ac_optarg"
    pkgbindir="$ac_optarg" ;;

  -datadir | --datadir | --datadi | --datad | --data | --dat | --da)
    ac_prev=datadir ;;
  -datadir=* | --datadir=* | --datadi=* | --datad=* | --data=* | --dat=* \
  | --da=*)
    datadir="$ac_optarg"
    pkgdatadir="$ac_optarg" ;;

  -exec-prefix | --exec_prefix | --exec-prefix | --exec-prefi \
  | --exec-pref | --exec-pre | --exec-pr | --exec-p | --exec- \
  | --exec | --exe | --ex)
    ac_prev=exec_prefix ;;
  -exec-prefix=* | --exec_prefix=* | --exec-prefix=* | --exec-prefi=* \
  | --exec-pref=* | --exec-pre=* | --exec-pr=* | --exec-p=* | --exec-=* \
  | --exec=* | --exe=* | --ex=*)
    exec_prefix="$ac_optarg" ;;

  -includedir | --includedir | --includedi | --included | --include \
  | --includ | --inclu | --incl | --inc)
    ac_prev=includedir ;;
  -includedir=* | --includedir=* | --includedi=* | --included=* | --include=* \
  | --includ=* | --inclu=* | --incl=* | --inc=*)
    includedir="$ac_optarg" ;;

  -infodir | --infodir | --infodi | --infod | --info | --inf)
    ac_prev=infodir ;;
  -infodir=* | --infodir=* | --infodi=* | --infod=* | --info=* | --inf=*)
    infodir="$ac_optarg" ;;

  -libdir | --libdir | --libdi | --libd)
    ac_prev=libdir ;;
  -libdir=* | --libdir=* | --libdi=* | --libd=*)
    libdir="$ac_optarg"
    pkglibdir="$ac_optarg" ;;

  -libexecdir | --libexecdir | --libexecdi | --libexecd | --libexec \
  | --libexe | --libex | --libe)
    ac_prev=libexecdir ;;
  -libexecdir=* | --libexecdir=* | --libexecdi=* | --libexecd=* | --libexec=* \
  | --libexe=* | --libex=* | --libe=*)
    libexecdir="$ac_optarg"
    pkglibexecdir="$ac_optarg" ;;

  -localstatedir | --localstatedir | --localstatedi | --localstated \
  | --localstate | --localstat | --localsta | --localst \
  | --locals | --local | --loca | --loc | --lo)
    ac_prev=localstatedir ;;
  -localstatedir=* | --localstatedir=* | --localstatedi=* | --localstated=* \
  | --localstate=* | --localstat=* | --localsta=* | --localst=* \
  | --locals=* | --local=* | --loca=* | --loc=* | --lo=*)
    localstatedir="$ac_optarg"
    pkglocalstatedir="$ac_optarg" ;;

  -mandir | --mandir | --mandi | --mand | --man | --ma | --m)
    ac_prev=mandir ;;
  -mandir=* | --mandir=* | --mandi=* | --mand=* | --man=* | --ma=* | --m=*)
    mandir="$ac_optarg" ;;

  -prefix | --prefix | --prefi | --pref | --pre | --pr | --p)
    ac_prev=prefix ;;
  -prefix=* | --prefix=* | --prefi=* | --pref=* | --pre=* | --pr=* | --p=*)
    prefix="$ac_optarg" ;;

  -runtimedir | --runtimedir | --runtimedi | --runtimed | --runtime | --runtim \
  | --runti | --runt | --run | --ru | --r)
    ac_prev=runtimedir ;;
  -runtimedir=* | --runtimedir=* | --runtimedi=* | --runtimed=* | --runtime=* \
  | --runtim=* | --runti=* | --runt=* | --run=* | --ru=* | --r=*)
    ac_prev=runtimedir ;;

  -sbindir | --sbindir | --sbindi | --sbind | --sbin | --sbi | --sb)
    ac_prev=sbindir ;;
  -sbindir=* | --sbindir=* | --sbindi=* | --sbind=* | --sbin=* \
  | --sbi=* | --sb=*)
    sbindir="$ac_optarg"
    pkgsbindir="$ac_optarg" ;;

  -sharedstatedir | --sharedstatedir | --sharedstatedi \
  | --sharedstated | --sharedstate | --sharedstat | --sharedsta \
  | --sharedst | --shareds | --shared | --share | --shar \
  | --sha | --sh)
    ac_prev=sharedstatedir ;;
  -sharedstatedir=* | --sharedstatedir=* | --sharedstatedi=* \
  | --sharedstated=* | --sharedstate=* | --sharedstat=* | --sharedsta=* \
  | --sharedst=* | --shareds=* | --shared=* | --share=* | --shar=* \
  | --sha=* | --sh=*)
    sharedstatedir="$ac_optarg" ;;

  -sysconfdir | --sysconfdir | --sysconfdi | --sysconfd | --sysconf \
  | --syscon | --sysco | --sysc | --sys | --sy)
    ac_prev=sysconfdir ;;
  -sysconfdir=* | --sysconfdir=* | --sysconfdi=* | --sysconfd=* | --sysconf=* \
  | --syscon=* | --sysco=* | --sysc=* | --sys=* | --sy=*)
    sysconfdir="$ac_optarg"
    pkgsysconfdir="$ac_optarg" ;;

  esac
done

# Be sure to have absolute paths.
for ac_var in exec_prefix prefix
do
  eval ac_val=$`echo $ac_var`
  case $ac_val in
    [[\\/$]]* | ?:[[\\/]]* | NONE | '' ) ;;
    *)  AC_MSG_ERROR([expected an absolute path for --$ac_var: $ac_val]);;
  esac
done

])dnl

dnl
dnl Support macro for AC_ARG_ENABLE
dnl Arguments:
dnl 1: Variable prefix
dnl 2: Variable stem
dnl The prefix is prepended with separating underscore to the stem
dnl to create the boolean variable to be set. The stem is also used
dnl to create the name of the AC_ARG_ENABLE variable and therefore
dnl must be the same as passed to AC_ARG_ENABLE. The prefix should
dnl be one of "use", "has", or "is", as is appropriate for the
dnl argument type. The target variable will be set to '1' if the
dnl enable argument is 'yes', and '0' otherwise.
dnl
dnl For instance, if the prefix is "has" and stem is "bob",
dnl then AC_ARG_ENABLE will set $enable_bob and this macro will set
dnl $has_bob based on the value in $enable_bob. See the examples
dnl in configure.ac.
dnl
dnl Note: As with AC_ARG_ENABLE, non-alphanumeric characters are
dnl transformed to underscores.
dnl
dnl This macro also AC_SUBST's the constructed variable name.
AC_DEFUN([TS_ARG_ENABLE_VAR],[
  tsl_prefix="AS_TR_SH($1)"
  tsl_stem="AS_TR_SH($2)"
  eval "tsl_enable=\$enable_${tsl_stem}"
  AS_IF([test "x$tsl_enable" = "xyes"],
     [eval "${tsl_prefix}_${tsl_stem}=1"],
     [eval "${tsl_prefix}_${tsl_stem}=0"]
  )
  AC_SUBST(m4_join([_], $1, AS_TR_SH($2)))
])

dnl TS_CHECK_SOCKOPT(socket-option, [action-if-found], [action-if-not-found]
AC_DEFUN([TS_CHECK_SOCKOPT], [
  AC_MSG_CHECKING([for $1 socket option])
  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
    ], [
    setsockopt(0, SOL_SOCKET, $1, (void*)0, 0);
    ])], [
    AC_MSG_RESULT(yes)
    $2
    ], [
    AC_MSG_RESULT(no)
    $3
  ])
])
