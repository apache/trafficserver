#!/bin/sh
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

# Show which platform we're actually building on
set +x
echo -n "Build platform: "

[ -f /etc/lsb-release ] && grep DISTRIB_RELEASE /etc/lsb-release
[ -f /etc/debian_version ] && cat /etc/debian_version
[ -f /etc/redhat-release ] && cat /etc/redhat-release

# Shouldn't have to tweak this
export ATS_SRC_HOME="/home/jenkins/src"

# Check if we're doing Debian style hardening
test "${JOB_NAME#*type=hardening}" != "${JOB_NAME}" && export DEB_BUILD_HARDENING=1

# Check if we need to use a different "make"
ATS_MAKE=make
test "${JOB_NAME#freebsd*}" != "${JOB_NAME}" && ATS_MAKE="gmake"
export ATS_MAKE

# Useful for timestamps etc. for daily runs
export TODAY=$(/bin/date +'%m%d%Y')

# Extract the current branch (default to master). ToDo: Can we do this better ?
ATS_BRANCH=master
ATS_IS_7="yes"

test "${JOB_NAME#*-5.3.x}" != "${JOB_NAME}" && ATS_BRANCH=5.3.x && ATS_IS_7="no"
test "${JOB_NAME#*-6.2.x}" != "${JOB_NAME}" && ATS_BRANCH=6.2.x && ATS_IS_7="no"
test "${JOB_NAME#*-7.1.x}" != "${JOB_NAME}" && ATS_BRANCH=7.1.x
test "${JOB_NAME#*-8.0.x}" != "${JOB_NAME}" && ATS_BRANCH=8.0.x
test "${JOB_NAME#*-8.1.x}" != "${JOB_NAME}" && ATS_BRANCH=8.1.x
test "${JOB_NAME#*-8.2.x}" != "${JOB_NAME}" && ATS_BRANCH=8.2.x
test "${JOB_NAME#*-8.3.x}" != "${JOB_NAME}" && ATS_BRANCH=8.3.x
test "${JOB_NAME#*-9.0.x}" != "${JOB_NAME}" && ATS_BRANCH=9.0.x
test "${JOB_NAME#*-9.1.x}" != "${JOB_NAME}" && ATS_BRANCH=9.1.x
test "${JOB_NAME#*-9.2.x}" != "${JOB_NAME}" && ATS_BRANCH=9.2.x
test "${JOB_NAME#*-9.3.x}" != "${JOB_NAME}" && ATS_BRANCH=9.3.x
test "${JOB_NAME#*-10.0.x}" != "${JOB_NAME}" && ATS_BRANCH=10.0.x
test "${JOB_NAME#*-10.1.x}" != "${JOB_NAME}" && ATS_BRANCH=10.1.x
test "${JOB_NAME#*-10.2.x}" != "${JOB_NAME}" && ATS_BRANCH=10.2.x
test "${JOB_NAME#*-10.3.x}" != "${JOB_NAME}" && ATS_BRANCH=10.3.x

export ATS_BRANCH
echo "Branch is $ATS_BRANCH"

# If the job name includes the string "clang", force clang. This can also be set
# explicitly for specific jobs.
test "${JOB_NAME#*compiler=clang}" != "${JOB_NAME}" && enable_clang=1

if [ "1" == "$enable_clang" ]; then
    if [ -x "/usr/local/bin/clang++50" ]; then
        # For FreeBSD 11.1 or earlier *NOT* recommended since libc++ is LLVM 4.0
        export CC="/usr/local/bin/clang50"
        export CXX="/usr/local/bin/clang++50"
    elif [ -x "/usr/bin/clang++-5.0" ]; then
        # For Ubuntu 17.x
        export CC="/usr/bin/clang-5.0"
        export CXX="/usr/bin/clang++-5.0"
    else
        export CC="clang"
        export CXX="clang++"
    fi
    export CXXFLAGS="-Qunused-arguments"
    export WITH_LIBCPLUSPLUS="yes"
elif [ "1" == "$enable_icc" ]; then
    source /opt/rh/devtoolset-7/enable
    source /opt/intel/bin/iccvars.sh intel64
    export CC=icc
    export CXX=icpc
else
    # Default is gcc / g++
    export CC=gcc
    export CXX=g++
    # Only test for non standard compilers on ATS v7.x and later. ToDo: Remove this when 6.x is EOLifed.
    if test "$ATS_IS_7" == "yes"; then
        if test -f "/opt/rh/devtoolset-7/enable"; then
            # This changes the path such that gcc / g++ is the right version. This is for CentOS 6 / 7.
            source /opt/rh/devtoolset-7/enable
            echo "Enabling devtoolset-7"
        elif test -x "/usr/bin/g++-7"; then
            # This is for Debian platforms
            export CC=/usr/bin/gcc-7
            export CXX=/usr/bin/g++-7
        fi
    fi
fi

# Echo out compiler information
echo "Compiler information:"
echo "CC: ${CC}"
$CC -v
echo "CXX: $CXX"
$CXX -v

# Figure out parallelism for regular builds / bots
ATS_MAKE_FLAGS="-j4"

# Restore verbose shell output
set -x
