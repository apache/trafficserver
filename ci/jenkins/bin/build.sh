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

# Check if it's a debug or release build
DEBUG=""
test "${JOB_NAME#*type=debug}" != "${JOB_NAME}" && DEBUG="--enable-debug"

# When to turn on ccache, disabled for some builds
CCACHE="--enable-ccache"
#test "${JOB_NAME#*debian_8}" != "${JOB_NAME}" && CCACHE=""
#test "${JOB_NAME#*ubuntu_12_04}" != "${JOB_NAME}" && CCACHE=""
#test "${JOB_NAME#*ubuntu_14_04}" != "${JOB_NAME}" && CCACHE=""
#test "${JOB_NAME#*compiler=clang,label=ubuntu_16_04}" != "${JOB_NAME}" && CCACHE=""

# When to enable -Werror, turned off for RHEL5 node (due to LuaJIT / gcc issues on RHEL5)
WERROR="--enable-werror"
test "${NODE_NAME#RHEL 5}" != "${NODE_NAME}" && WERROR=""

# Change to the build area (this is previously setup in extract.sh)
cd "${WORKSPACE}/${BUILD_NUMBER}/build"

mkdir -p BUILDS && cd BUILDS
../configure \
    --prefix="${WORKSPACE}/${BUILD_NUMBER}/install" \
    --enable-experimental-plugins \
    --enable-example-plugins \
    --with-user=jenkins \
    ${CCACHE} \
    ${WERROR} \
    ${DEBUG}

${ATS_MAKE} ${ATS_MAKE_FLAGS} V=1 Q=
