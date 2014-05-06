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

# Parse debug / release
enable_debug=""
test "${JOB_NAME#*type=debug}" != "${JOB_NAME}" && enable_debug="--enable-debug"

# Parse compiler, only turn on ccache with gcc (clang fails, sigh)
enable_ccache=""
test "${JOB_NAME#*compiler=gcc}" != "${JOB_NAME}" && enable_ccache="--enable-ccache"

# Change to the build area (this is previously setup in extract.sh)
cd "${WORKSPACE}/${BUILD_NUMBER}/build"

./configure \
    --prefix="${WORKSPACE}/${BUILD_NUMBER}/install" \
    --enable-werror \
    --enable-experimental-plugins \
    --enable-example-plugins \
    --enable-test-tools \
    ${enable_ccache} \
    ${enable_debug}

make -j4 V=1
