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

cd "${WORKSPACE}/src"
[ -d tests ] || exit 0

autoreconf -if

INSTALL="${WORKSPACE}/${BUILD_NUMBER}/install"
SANDBOX="/tmp/ausb-$$"

mkdir -p $INSTALL

./configure --prefix="$INSTALL" \
            --with-user=jenkins \
            --enable-experimental-plugins \
            --enable-example-plugins \
            --enable-ccache \
            --enable-debug \
            --enable-werror

# Build and run regressions
${ATS_MAKE} ${ATS_MAKE_FLAGS} V=1 Q=
${ATS_MAKE} check VERBOSE=Y && ${ATS_MAKE} install

/usr/bin/autest -D ./tests/gold_tests --sandbox "$SANDBOX" --ats-bin "${INSTALL}/bin"
status="$?"
[ -d "$SANDBOX" ] && rm -rf "$SANDBOX"

[ "0" != "$status" ] && exit -1
exit 0
