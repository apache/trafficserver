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

set +x

# Check if it's a debug or release build
DEBUG=""
test "${JOB_NAME#*type=debug}" != "${JOB_NAME}" && DEBUG="--enable-debug"

# When to turn on ccache, disabled for some builds
CCACHE="--enable-ccache"

# When to enable -Werror
WERROR="--enable-werror"

echo "DEBUG: $DEBUG"
echo "CCACHE: $CCACHE"
echo "WERROR: $WERROR"

# Change to the build area (this is previously setup in extract.sh)
cd "${WORKSPACE}/${BUILD_NUMBER}/build"
mkdir -p BUILDS && cd BUILDS

# Restore verbose shell output
set -x

../configure \
    --prefix="${WORKSPACE}/${BUILD_NUMBER}/install" \
    --enable-experimental-plugins \
    --enable-example-plugins \
    --with-user=jenkins \
    ${CCACHE} \
    ${WERROR} \
    ${DEBUG}

echo -n "Main build started at " && date
${ATS_MAKE} ${ATS_MAKE_FLAGS} V=1 Q= || exit 1
echo -n "Main build finished at " && date
