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

# Get the Coverity tools into our path
source /opt/rh/devtoolset-7/enable
export PATH=/home/coverity/bin:${PATH}

COV_TARBALL=/tmp/trafficserver-${TODAY}.tgz
COV_VERSION=$(git rev-parse --short HEAD)

autoreconf -fi
./configure --enable-experimental-plugins --enable-wccp

cov-build --dir cov-int ${ATS_MAKE} -j4 V=1
tar czvf ${COV_TARBALL} cov-int

# Now submit this artifact
/home/admin/bin/cov-submit.sh ${COV_TARBALL} ${COV_VERSION}

# Cleanup
rm -rf cov-int
rm ${COV_TARBALL}

${ATS_MAKE} distclean
