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

# Test if we should enable CPPAPI (only 5.0 and later for now)
enable_cppapi="--enable-cppapi"
test "${JOB_NAME#*-4.2.x}" != "${JOB_NAME}" && enable_cppapi=""

# Where do we run this?
TS_PREFIX="/opt/jenkins/${JOB_NAME}"
TSQA_TSXS=${TS_PREFIX}/bin/tsxs; export TSQA_TSXS

cd "${WORKSPACE}/src"

rm -rf ${TS_PREFIX}

# This needs to be added back when we resolve all debug build issues
autoreconf -fi
./configure \
    --prefix=${TS_PREFIX} \
    --enable-debug \
    --enable-ccache \
    --enable-werror \
    --enable-experimental-plugins \
    --enable-test-tools \
    ${enable_cppapi}

${ATS_MAKE} -j8
${ATS_MAKE} install
${ATS_MAKE} distclean

# Run all the TSQA tests. We skip a couple since they can not succeed from the CI
cd ci/tsqa || exit 2
rm -rf /tmp/test-*.[0-9]*
./run_all.sh -e test-multicert-loading -e test-privilege-elevation
status=$?

# Exit with proper status, right now there's only one test, but still
exit $status
