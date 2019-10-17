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

PREFIX="${WORKSPACE}/${BUILD_NUMBER}/install"
REMAP="${PREFIX}/etc/trafficserver/remap.config"
RECORDS="${PREFIX}/etc/trafficserver/records.config"

TWEAK=""
[ "1" == "$enable_tweak" ] && TWEAK="-tweak"

# Change to the build area (this is previously setup in extract.sh)
set +x
cd "${WORKSPACE}/${BUILD_NUMBER}/build"

./configure \
    --prefix=${PREFIX} \
    --with-user=jenkins \
    --enable-ccache

# Not great, but these can fail on the "docs' builds for older versions, sigh
${ATS_MAKE} -i ${ATS_MAKE_FLAGS} V=1 Q=
${ATS_MAKE} -i install

[ -x ${PREFIX}/bin/traffic_server ] || exit 1

# Get NPM v10
source /opt/rh/rh-nodejs10/enable

# Setup and start ATS with the required remap rule
echo "map http://127.0.0.1:8080 http://192.168.3.1:8000" >> $REMAP

${PREFIX}/bin/trafficserver start

set -x

cd /home/jenkins/cache-tests
npm run --silent cli --base=http://127.0.0.1:8080/ > /CA/cache-tests/${ATS_BRANCH}.json
cat /CA/cache-tests/${ATS_BRANCH}.json

${PREFIX}/bin/trafficserver stop


# Now run it again, maybe, with the tweaked configs
if [ "" != "$TWEAK" ]; then
#    echo "CONFIG proxy.config.http.cache.required_headers INT 1" >> $RECORDS
    echo "CONFIG proxy.config.http.negative_caching_enabled INT 1" >> $RECORDS
    ${PREFIX}/bin/trafficserver start
    cd /home/jenkins/cache-tests
    npm run --silent cli --base=http://127.0.0.1:8080/ > /CA/cache-tests/${ATS_BRANCH}${TWEAK}.json
    echo "TWEAKED RESULTS"
    cat /CA/cache-tests/${ATS_BRANCH}${TWEAK}.json

    ${PREFIX}/bin/trafficserver stop
fi

# We should check the .json file here, but not yet
exit 0
