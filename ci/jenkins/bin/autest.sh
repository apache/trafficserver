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

[ -d tests ] || exit 0

# Setup autoconf
INSTALL="${WORKSPACE}/${BUILD_NUMBER}/install"
mkdir -p $INSTALL
cd src
autoreconf -if

URL="https://ci.trafficserver.apache.org/files/autest"
AUSB="ausb-${ghprbPullId}.${BUILD_NUMBER}"
SANDBOX="/var/tmp/${AUSB}"

./configure --prefix="${INSTALL}" \
            --with-user=jenkins \
            --enable-experimental-plugins \
            --enable-example-plugins \
            --enable-ccache \
            --enable-debug \
	    --enable-wccp \
            --enable-werror

# Build and run regressions
${ATS_MAKE} -j4 && ${ATS_MAKE} install
${INSTALL}/bin/traffic_server -K -k -R 1
[ "0" != "$?" ] && exit -1

# Now run autest
AUTEST="/usr/bin/autest"
[ ! -x ${AUTEST} ] && AUTEST="/usr/local/bin/autest"

${AUTEST} -D ./tests/gold_tests --sandbox "$SANDBOX" --ats-bin "${INSTALL}/bin"
status=$?

# Cleanup
cd /var/tmp # To be safer
chmod -R a+r ${SANDBOX}
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
