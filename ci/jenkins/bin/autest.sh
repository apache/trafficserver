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

cd src
sleep 30

git branch --contains ${ghprbActualCommit} > /dev/null
if [ $? = 0 -a ! -z "$ghprbActualCommit" ]; then
    git diff ${ghprbActualCommit}^...${ghprbActualCommit} --name-only | egrep -E '^(build|iocore|proxy|tests|include|mgmt|plugins|proxy|src)/' > /dev/null
    if [ $? = 1 ]; then
        echo "No relevant files changed, skipping run"
        exit 0
    fi
fi

# Set default encoding UTF-8 for AuTest
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
echo "LC_ALL: $LC_ALL"
echo "LANG: $LANG"

# Check python version & encoding
python3 --version
echo "python default encoding: "
python3 -c "import sys; print(sys.getdefaultencoding())"

INSTALL="${ATS_BUILD_BASEDIR}/install"
URL="https://ci.trafficserver.apache.org/autest"
JOB_ID=${ghprbPullId:-${ATS_BRANCH:-master}}
AUSB="ausb-${JOB_ID}.${BUILD_NUMBER}"
SANDBOX="/var/tmp/${AUSB}"
PROXY_VERIFIER_VERSIONS="/home/jenkins/proxy-verifier"
PROXY_VERIFIER_VERSION_FILE="tests/proxy-verifier-version.txt"
PROXY_VERIFIER_PREPARE="tests/prepare_proxy_verifier.sh"

# Optional settings
CCACHE=""
WERROR=""
DEBUG=""
WCCP=""
LUAJIT=""
QUIC=""
CURL=""
AUTEST_DEBUG=""
AUTEST_VERBOSE=""
PROXY_VERIFIER_ARGUMENT=""

[ "1" == "$enable_ccache" ] && CCACHE="--enable-ccache"
[ "1" == "$enable_werror" ] && WERROR="--enable-werror"
[ "1" == "$enable_debug" ] && DEBUG="--enable-debug"
[ "1" == "$enable_wccp" ] && WCCP="--enable-wccp"
[ "1" == "$enable_luajit" ] && LUAJIT="--enable-luajit"
[ "1" == "$enable_quic" ] && QUIC="--with-openssl=/opt/openssl-quic"
[ "1" == "$disable_curl" ] && CURL="--disable-curl"
[ "1" == "$enable_autest_debug" ] && AUTEST_DEBUG="--debug"
[ "1" == "$enable_autest_verbose" ] && AUTEST_VERBOSE="--verbose"

mkdir -p ${INSTALL}

# The tests directory must exist (i.e. for older branches we don't run this)
[ -d tests ] || exit 0

echo "CCACHE: $CCACHE"
echo "WERROR: $WERROR"
echo "DEBUG: $DEBUG"
echo "WCCP: $WCCP"
echo "LUAJIT: $LUAJIT"
echo "QUIC: $QUIC"
echo "CURL: $CURL"

# Restore verbose shell output
set -x

# Configure
autoreconf -if
./configure \
    --prefix="${INSTALL}" \
    --with-user=jenkins \
    --enable-experimental-plugins \
    --enable-example-plugins \
    ${CCACHE} \
    ${WCCP} \
    ${LUAJIT} \
    ${QUIC} \
    ${WERROR} \
    ${DEBUG} \
    ${CURL}

# Build and run regressions
${ATS_MAKE} -j4 && ${ATS_MAKE} install
[ -x ${INSTALL}/bin/traffic_server ] || exit -1

# Now run autest
set +x
echo -n "=======>>>>  Started on "
date

AUTEST="/usr/bin/autest"
[ ! -x ${AUTEST} ] && AUTEST="/usr/local/bin/autest"
set -x

pv_version=""
if [ -f "${PROXY_VERIFIER_VERSION_FILE}" ]; then
  pv_version=`cat "${PROXY_VERIFIER_VERSION_FILE}"`
elif [ -f "${PROXY_VERIFIER_PREPARE}" ]; then
  pv_version=`awk -F'"' '/^pv_version/ {print $2}' "${PROXY_VERIFIER_PREPARE}"`
fi
if [ "x${pv_version}" != "x" ]; then
  PROXY_VERIFIER_BIN="${PROXY_VERIFIER_VERSIONS}/${pv_version}/bin"
  PROXY_VERIFIER_ARGUMENT="--proxy-verifier-bin ${PROXY_VERIFIER_BIN}"
fi

${AUTEST} \
    -D ./tests/gold_tests \
    --sandbox "$SANDBOX" \
    --ats-bin "${INSTALL}/bin" \
    $PROXY_VERIFIER_ARGUMENT \
    $AUTEST_DEBUG \
    $AUTEST_VERBOSE
status=$?

set +x
echo -n "=======<<<<  Finished on "
date

# Cleanup
cd /var/tmp # To be safer
chmod -R a+r ${SANDBOX}
if [ "0" != "$status" ]; then
    if [ -d "$SANDBOX" ]; then
        find "$SANDBOX" -name \*.db -exec rm {} \;
        mv "$SANDBOX" /CA/autest
        echo "Sandbox is available at ${URL}/${AUSB}/"
    fi
    exit -1
else
    [ -d "$SANDBOX" ] && rmdir "$SANDBOX"
    exit 0
fi

set -x
