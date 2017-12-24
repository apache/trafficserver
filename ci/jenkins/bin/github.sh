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

INSTALL="${WORKSPACE}/${BUILD_NUMBER}/install"

# Optional settings
CCACHE=""; WERROR=""; DEBUG=""; WCCP=""
[ "1" == "$enable_ccache" ] && CCACHE="--enable-ccache"
[ "1" == "$enable_werror" ] && WERROR="--enable-werror"
[ "1" == "$enable_debug" ] && DEBUG="--enable-debug"
[ "1" == "$enable_wccp" ] && WCCP="--enable-wccp"

# Check for clang
if [ "1" == "$enable_clang" ]; then
    export CC="clang"
    export CXX="clang++"
    export CXXFLAGS="-Qunused-arguments -std=c++11"
    export WITH_LIBCPLUSPLUS="yes"
fi

mkdir -p ${INSTALL}
cd src
autoreconf -if

./configure --prefix="${INSTALL}" \
            --with-user=jenkins \
            --enable-experimental-plugins \
            --enable-example-plugins \
            ${CCACHE} \
            ${WCCP} \
            ${WERROR} \
            ${DEBUG}

# Build and run regressions
${ATS_MAKE} ${ATS_MAKE_FLAGS} V=1 Q=
${ATS_MAKE} check VERBOSE=Y V=1 && ${ATS_MAKE} install

${INSTALL}/bin/traffic_server -K -k -R 1
[ "0" != "$?" ] && exit -1

exit 0
