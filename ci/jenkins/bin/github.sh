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

INSTALL="${WORKSPACE}/${BUILD_NUMBER}/install"

# Optional settings
CCACHE=""
WERROR=""
DEBUG=""
WCCP=""
LUAJIT=""
[ "1" == "$enable_ccache" ] && CCACHE="--enable-ccache"
[ "1" == "$enable_werror" ] && WERROR="--enable-werror"
[ "1" == "$enable_debug" ] && DEBUG="--enable-debug"
[ "1" == "$enable_wccp" ] && WCCP="--enable-wccp"
[ "1" == "$enable_luajit" ] && LUAJIT="--enable-luajit"

mkdir -p ${INSTALL}
cd src

echo "CCACHE: $CCACHE"
echo "WERROR: $WERROR"
echo "DEBUG: $DEBUG"
echo "WCCP: $WCCP"
echo "LUAJIT: $LUAJIT"

# Restore verbose shell output
set -x

# Configure
autoreconf -if

./configure --prefix="${INSTALL}" \
    --with-user=jenkins \
    --enable-experimental-plugins \
    --enable-example-plugins \
    ${CCACHE} \
    ${WCCP} \
    ${LUAJIT} \
    ${WERROR} \
    ${DEBUG}

# Build and run regressions
${ATS_MAKE} ${ATS_MAKE_FLAGS} V=1 Q=
${ATS_MAKE} check VERBOSE=Y V=1 && ${ATS_MAKE} install

${INSTALL}/bin/traffic_server -K -k -R 1
[ "0" != "$?" ] && exit -1

exit 0
