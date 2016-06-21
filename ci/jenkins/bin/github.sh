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

# Setup autoconf
cd src
autoreconf -if

mkdir -p "${WORKSPACE}/${BUILD_NUMBER}/install"

./configure --prefix="${WORKSPACE}/${BUILD_NUMBER}/install" \
            --enable-experimental-plugins \
            --enable-example-plugins \
            --enable-ccache \
            --enable-debug \
            --enable-werror \
            --enable-cppapi


# Test clang-format (but only where we have the local copy of clang-format, i.e. linux)
if [ -d /usr/local/fmt ]; then
    [ ! -d .git/fmt ] && cp -rp /usr/local/fmt .git
    make clang-format
    git diff --exit-code
    [ "0" != "$?" ] && exit -1
fi

# Build and run regressions
make -j4
make check VERBOSE=Y && make install

"${WORKSPACE}/${BUILD_NUMBER}/install/bin/traffic_server" -K -k -R 1
