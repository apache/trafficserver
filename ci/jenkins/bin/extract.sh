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
# Setup the build and install area for this build
mkdir -p "${WORKSPACE}/${BUILD_NUMBER}/build"
mkdir -p "${WORKSPACE}/${BUILD_NUMBER}/install"

cd "${WORKSPACE}/${BUILD_NUMBER}/build"
echo "Artifact: ${ATS_SRC_HOME}/trafficserver-${ATS_BRANCH}.tar.bz2"
tar xf ${ATS_SRC_HOME}/trafficserver-${ATS_BRANCH}.tar.bz2
mv trafficserver-*/* .

echo "build: ${WORKSPACE}/${BUILD_NUMBER}/build"
echo "install: ${WORKSPACE}/${BUILD_NUMBER}/install"

# Restore verbose shell output
set -x
