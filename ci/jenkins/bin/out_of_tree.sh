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

# This does intentionally not run the regressions, it's primarily a "build" test

cd "${WORKSPACE}/src"

autoreconf -fi
mkdir -p BUILDS && cd BUILDS
../configure \
    --enable-ccache \
    --enable-werror \
    --enable-experimental-plugins \
    --enable-example-plugins \
    --enable-test-tools

${ATS_MAKE} -j5 V=1
${ATS_MAKE} check
${ATS_MAKE} clean
