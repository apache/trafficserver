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
test "${JOB_NAME#*-4.2.x}" != "${JOB_NAME}" && ATS_BRANCH=4.2.x
test "${JOB_NAME#*-5.0.x}" != "${JOB_NAME}" && ATS_BRANCH=5.0.x
test "${JOB_NAME#*-5.1.x}" != "${JOB_NAME}" && ATS_BRANCH=5.1.x
test "${JOB_NAME#*-5.2.x}" != "${JOB_NAME}" && ATS_BRANCH=5.2.x
test "${JOB_NAME#*-5.3.x}" != "${JOB_NAME}" && ATS_BRANCH=5.3.x
export ATS_BRANCH

# Decide on compilers, gcc is the default
if test "${JOB_NAME#*compiler=clang}" != "${JOB_NAME}"; then
    export CC="clang"
    export CXX="clang++"
    export CXXFLAGS="-Qunused-arguments -std=c++11"
    export WITH_LIBCPLUSPLUS="yes"
fi
