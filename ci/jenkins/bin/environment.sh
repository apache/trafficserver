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

test "${JOB_NAME#*-6.2.x}" != "${JOB_NAME}" && ATS_BRANCH=6.2.x && ATS_IS_7="no"
test "${JOB_NAME#*-7.1.x}" != "${JOB_NAME}" && ATS_BRANCH=7.1.x
test "${JOB_NAME#*-7.2.x}" != "${JOB_NAME}" && ATS_BRANCH=7.2.x
test "${JOB_NAME#*-8.0.x}" != "${JOB_NAME}" && ATS_BRANCH=8.0.x
test "${JOB_NAME#*-8.1.x}" != "${JOB_NAME}" && ATS_BRANCH=8.1.x
test "${JOB_NAME#*-8.2.x}" != "${JOB_NAME}" && ATS_BRANCH=8.2.x
test "${JOB_NAME#*-9.0.x}" != "${JOB_NAME}" && ATS_BRANCH=9.0.x
test "${JOB_NAME#*-9.1.x}" != "${JOB_NAME}" && ATS_BRANCH=9.1.x
test "${JOB_NAME#*-9.2.x}" != "${JOB_NAME}" && ATS_BRANCH=9.1.x

export ATS_BRANCH
echo "Branch is $ATS_BRANCH"

# Decide on compilers, gcc is the default
if test "${JOB_NAME#*compiler=clang}" != "${JOB_NAME}"; then
	export CC="clang"
	export CXX="clang++"
	#    export CXXFLAGS="-Qunused-arguments -std=c++11"
	export CXXFLAGS="-Qunused-arguments"
	export WITH_LIBCPLUSPLUS="yes"
fi

# Check for devtoolset-7, but only for ATS 7.x and later
if test "$ATS_IS_7" == "yes"; then
	if test -f "/opt/rh/devtoolset-7/enable"; then
		source /opt/rh/devtoolset-7/enable
		echo "Enabling devtoolset-7"
	elif test -x "/usr/bin/gcc-4.9"; then
		export CC="/usr/bin/gcc-4.9"
		export CXX="/usr/bin/g++-4.9"
		echo "CC: $CC"
		echo "CXX: $CXX"
	fi
fi

# Figure out parallelism for regular builds / bots
ATS_MAKE_FLAGS="-j4"

# Restore verbose shell output
set -x
