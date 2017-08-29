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
[ -d tests ] || exit 0

autoreconf -if

INSTALL="${WORKSPACE}/${BUILD_NUMBER}/install"

URL="https://ci.trafficserver.apache.org/files/autest"
AUSB="ausb-${ghprbPullId}.${BUILD_NUMBER}"
SANDBOX="/var/tmp/${AUSB}"

mkdir -p $INSTALL

./configure --prefix="$INSTALL" \
            --with-user=jenkins \
            --enable-experimental-plugins \
            --enable-ccache \
            --enable-debug \
            --enable-werror

# Build and run regressions
${ATS_MAKE} ${ATS_MAKE_FLAGS} V=1 Q= || exit -1
${ATS_MAKE} install
/usr/bin/autest -D ./tests/gold_tests --sandbox "$SANDBOX" --ats-bin "${INSTALL}/bin"
status=$?

# Cleanup
cd /var/tmp # To be safer
if [ "0" != "$status" ]; then
    if [ -d "$SANDBOX" ]; then
        find "$SANDBOX" -name \*.db  -exec rm {} \;
        mv "$SANDBOX" /CA/autest
        echo "Sandbox is available at ${URL}/${AUSB}/"
    fi
    exit -1
else
    [ -d "$SANDBOX" ] && rmdir "$SANDBOX"
    exit 0
fi
