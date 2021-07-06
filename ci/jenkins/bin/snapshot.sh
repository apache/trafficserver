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

autoreconf -fi

./configure \
    --with-user=jenkins \
    --enable-ccache \
    --enable-debug \
    --enable-werror \
    --enable-experimental-plugins \
    --enable-example-plugins

${ATS_MAKE} asf-dist

# Make an "atomic" copy of the artifact (and leave it here for the archive)
cp trafficserver-*.tar.bz2 ${ATS_SRC_HOME}/trafficserver-${ATS_BRANCH}.tar.bz2.new
mv ${ATS_SRC_HOME}/trafficserver-${ATS_BRANCH}.tar.bz2.new ${ATS_SRC_HOME}/trafficserver-${ATS_BRANCH}.tar.bz2

# Make sure we get a chance to synchronize the /CA/src directory to remote sites.
sleep 5
