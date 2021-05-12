#!/usr/bin/env bash
#
#  Simple script to build OpenSSL and various tools with H3 and QUIC support.
#  This probably needs to be modified based on platform.
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

set -e

# Update this as the draft we support updates.
OPENSSL_BRANCH=${OPENSSL_BRANCH:-"OpenSSL_1_1_1k+quic"}

# Set these, if desired, to change these to your preferred installation
# directory
BASE=${BASE:-"/opt"}
OPENSSL_BASE=${OPENSSL_BASE:-"${BASE}/openssl-quic"}
OPENSSL_PREFIX=${OPENSSL_PREFIX:-"${OPENSSL_BASE}-${OPENSSL_BRANCH}"}
MAKE="make"

# These are for Linux like systems, specially the LDFLAGS, also depends on dirs above
CFLAGS=${CFLAGS:-"-O3 -g"}
CXXFLAGS=${CXXFLAGS:-"-O3 -g"}
LDFLAGS=${LDFLAGS:-"-Wl,-rpath=${OPENSSL_PREFIX}/lib"}

if [ -e /etc/redhat-release ]; then
    MAKE="gmake"
    echo "+-------------------------------------------------------------------------+"
    echo "| You probably need to run this, or something like this, for your system: |"
    echo "|                                                                         |"
    echo "|   sudo yum -y install libev-devel jemalloc-devel python2-devel          |"
    echo "|   sudo yum -y install libxml2-devel c-ares-devel libevent-devel         |"
    echo "|   sudo yum -y install jansson-devel zlib-devel systemd-devel            |"
    echo "+-------------------------------------------------------------------------+"
    echo
    echo
elif [ -e /etc/debian_version ]; then
    echo "+-------------------------------------------------------------------------+"
    echo "| You probably need to run this, or something like this, for your system: |"
    echo "|                                                                         |"
    echo "|   sudo apt -y install libev-dev libjemalloc-dev python2-dev libxml2-dev |"
    echo "|   sudo apt -y install libpython2-dev libc-ares-dev libsystemd-dev       |"
    echo "|   sudo apt -y install libevent-dev libjansson-dev zlib1g-dev            |"
    echo "+-------------------------------------------------------------------------+"
    echo
    echo
fi

set -x

# OpenSSL needs special hackery ... Only grabbing the branch we need here... Bryan has shit for network.
echo "Building OpenSSL with QUIC support"
[ ! -d openssl-quic ] && git clone -b ${OPENSSL_BRANCH} --depth 1 https://github.com/quictls/openssl.git openssl-quic
cd openssl-quic
./config --prefix=${OPENSSL_PREFIX}
${MAKE} -j $(nproc)
sudo ${MAKE} install

# The symlink target provides a more convenient path for the user while also
# providing, in the symlink source, the precise branch of the OpenSSL build.
sudo ln -sf ${OPENSSL_PREFIX} ${OPENSSL_BASE}
cd ..

# Then nghttp3
echo "Building nghttp3..."
[ ! -d nghttp3 ] && git clone https://github.com/ngtcp2/nghttp3.git
cd nghttp3
autoreconf -if
./configure --prefix=${BASE} PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${OPENSSL_PREFIX}/lib/pkgconfig CFLAGS="${CFLAGS}" CXXFLAGS="${CXXFLAGS}" LDFLAGS="${LDFLAGS}"
${MAKE} -j $(nproc)
sudo ${MAKE} install
cd ..

# Now ngtcp2
echo "Building ngtcp2..."
[ ! -d ngtcp2 ] && git clone https://github.com/ngtcp2/ngtcp2.git
cd ngtcp2
autoreconf -if
./configure --prefix=${BASE} PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${OPENSSL_PREFIX}/lib/pkgconfig CFLAGS="${CFLAGS}" CXXFLAGS="${CXXFLAGS}" LDFLAGS="${LDFLAGS}"
${MAKE} -j $(nproc)
sudo ${MAKE} install
cd ..

# Then nghttp2, with support for H3
echo "Building nghttp2 ..."
[ ! -d nghttp2 ] && git clone https://github.com/tatsuhiro-t/nghttp2.git
cd nghttp2
git checkout --track -b quic origin/quic
autoreconf -if
./configure --prefix=${BASE} PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${OPENSSL_PREFIX}/lib/pkgconfig CFLAGS="${CFLAGS}" CXXFLAGS="${CXXFLAGS}" LDFLAGS="${LDFLAGS}"
${MAKE} -j $(nproc)
sudo ${MAKE} install
cd ..

# And finally curl
echo "Building curl ..."
[ ! -d curl ] && git clone https://github.com/curl/curl.git
cd curl
autoreconf -i
./configure --prefix=${BASE} --with-ssl=${OPENSSL_PREFIX} --with-nghttp2=${BASE} --with-nghttp3=${BASE} --with-ngtcp2=${BASE} CFLAGS="${CFLAGS}" CXXFLAGS="${CXXFLAGS}" LDFLAGS="${LDFLAGS}"
${MAKE} -j $(nproc)
sudo ${MAKE} install
