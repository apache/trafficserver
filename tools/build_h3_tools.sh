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

WORKDIR="$(mktemp -d)"
readonly WORKDIR

cd "${WORKDIR}"
echo "Building H3 dependencies in ${WORKDIR} ..."

# Update this as the draft we support updates.
OPENSSL_BRANCH=${OPENSSL_BRANCH:-"openssl-3.1.2+quic"}

# Set these, if desired, to change these to your preferred installation
# directory
BASE=${BASE:-"/opt/ats_h3_tools"}
OPENSSL_BASE=${OPENSSL_BASE:-"${BASE}/openssl-quic"}
OPENSSL_PREFIX=${OPENSSL_PREFIX:-"${OPENSSL_BASE}-${OPENSSL_BRANCH}"}
MAKE="make"

CFLAGS=${CFLAGS:-"-O3 -g"}
CXXFLAGS=${CXXFLAGS:-"-O3 -g"}

if [ -e /etc/redhat-release ]; then
    MAKE="gmake"
    TMP_QUICHE_BSSL_PATH="${BASE}/boringssl/lib64"
    echo "+-------------------------------------------------------------------------+"
    echo "| You probably need to run this, or something like this, for your system: |"
    echo "|                                                                         |"
    echo "|   sudo yum -y install libev-devel jemalloc-devel python2-devel          |"
    echo "|   sudo yum -y install libxml2-devel c-ares-devel libevent-devel         |"
    echo "|   sudo yum -y install jansson-devel zlib-devel systemd-devel cargo      |"
    echo "|                                                                         |"
    echo "| Rust may be needed too, see https://rustup.rs for the details           |"
    echo "+-------------------------------------------------------------------------+"
    echo
    echo
elif [ -e /etc/debian_version ]; then
    TMP_QUICHE_BSSL_PATH="${BASE}/boringssl/lib"
    echo "+-------------------------------------------------------------------------+"
    echo "| You probably need to run this, or something like this, for your system: |"
    echo "|                                                                         |"
    echo "|   sudo apt -y install libev-dev libjemalloc-dev python2-dev libxml2-dev |"
    echo "|   sudo apt -y install libpython2-dev libc-ares-dev libsystemd-dev       |"
    echo "|   sudo apt -y install libevent-dev libjansson-dev zlib1g-dev cargo      |"
    echo "|                                                                         |"
    echo "| Rust may be needed too, see https://rustup.rs for the details           |"
    echo "+-------------------------------------------------------------------------+"
    echo
    echo
fi

if [ `uname -s` = "Darwin" ]; then
    echo "+-------------------------------------------------------------------------+"
    echo "| When building on a Mac, be aware that the Apple version of clang may    |"
    echo "| fail to build curl due to the issue described here:                     |"
    echo "| https://github.com/curl/curl/issues/11391#issuecomment-1623890325       |"
    echo "+-------------------------------------------------------------------------+"
fi

if [ -z ${QUICHE_BSSL_PATH+x} ]; then
   QUICHE_BSSL_PATH=${TMP_QUICHE_BSSL_PATH:-"${BASE}/boringssl/lib"}
fi

set -x
if [ `uname -s` = "Linux" ]
then
  num_threads=$(nproc)
elif [ `uname -s` = "FreeBSD" ]
then
  num_threads=$(sysctl -n hw.ncpu)
else
  # MacOS.
  num_threads=$(sysctl -n hw.logicalcpu)
fi

# boringssl
echo "Building boringssl..."

# We need this go version.
sudo mkdir -p ${BASE}/go

if [ `uname -m` = "arm64" -o `uname -m` = "aarch64" ]; then
    ARCH="arm64"
else
    ARCH="amd64"
fi

if [ `uname -s` = "Darwin" ]; then
    OS="darwin"
elif [ `uname -s` = "FreeBSD" ]; then
    OS="freebsd"
else
    OS="linux"
fi

wget https://go.dev/dl/go1.21.0.${OS}-${ARCH}.tar.gz
sudo rm -rf ${BASE}/go && sudo tar -C ${BASE} -xf go1.21.0.${OS}-${ARCH}.tar.gz
rm go1.21.0.${OS}-${ARCH}.tar.gz
sudo chmod -R a+rX ${BASE}

GO_BINARY_PATH=${BASE}/go/bin/go
if [ ! -d boringssl ]; then
  git clone https://boringssl.googlesource.com/boringssl
  cd boringssl
  git checkout e4f60679caa293c047be69f57fc48b46c7452327
  cd ..
fi
cd boringssl
cmake \
  -B build \
  -DGO_EXECUTABLE=${GO_BINARY_PATH} \
  -DCMAKE_INSTALL_PREFIX=${BASE}/boringssl \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS='-Wno-error=ignored-attributes' \
  -DBUILD_SHARED_LIBS=1
cmake --build build -j ${num_threads}
sudo cmake --install build
sudo chmod -R a+rX ${BASE}
cd ..

# Build quiche
# Steps borrowed from: https://github.com/apache/trafficserver-ci/blob/main/docker/rockylinux8/Dockerfile
echo "Building quiche"
QUICHE_BASE="${BASE:-/opt}/quiche"
[ ! -d quiche ] && git clone --recursive https://github.com/cloudflare/quiche.git
cd quiche
# Latest quiche commits breaks our code so we build from the last commit
# we know it works, in this case this commit includes the rpath fix commit
# for quiche. https://github.com/cloudflare/quiche/pull/1508
# Why does the latest break our code? -> https://github.com/cloudflare/quiche/pull/1537
git checkout a1b212761c6cc0b77b9121cdc313e507daf6deb3
QUICHE_BSSL_PATH=${QUICHE_BSSL_PATH} QUICHE_BSSL_LINK_KIND=dylib cargo build -j4 --package quiche --release --features ffi,pkg-config-meta,qlog
sudo mkdir -p ${QUICHE_BASE}/lib/pkgconfig
sudo mkdir -p ${QUICHE_BASE}/include
sudo cp target/release/libquiche.a ${QUICHE_BASE}/lib/
[ -f target/release/libquiche.so ] && sudo cp target/release/libquiche.so ${QUICHE_BASE}/lib/
sudo cp quiche/include/quiche.h ${QUICHE_BASE}/include/
sudo cp target/release/quiche.pc ${QUICHE_BASE}/lib/pkgconfig
sudo chmod -R a+rX ${BASE}
cd ..

echo "Building OpenSSL with QUIC support"
[ ! -d openssl-quic ] && git clone -b ${OPENSSL_BRANCH} --depth 1 https://github.com/quictls/openssl.git openssl-quic
cd openssl-quic
./config enable-tls1_3 --prefix=${OPENSSL_PREFIX}
${MAKE} -j ${num_threads}
sudo ${MAKE} install_sw
sudo chmod -R a+rX ${BASE}

# The symlink target provides a more convenient path for the user while also
# providing, in the symlink source, the precise branch of the OpenSSL build.
sudo ln -sf ${OPENSSL_PREFIX} ${OPENSSL_BASE}
sudo chmod -R a+rX ${BASE}
cd ..

# OpenSSL will install in /lib or lib64 depending upon the architecture.
if [ -d "${OPENSSL_PREFIX}/lib" ]; then
  OPENSSL_LIB="${OPENSSL_PREFIX}/lib"
elif [ -d "${OPENSSL_PREFIX}/lib64" ]; then
  OPENSSL_LIB="${OPENSSL_PREFIX}/lib64"
else
  echo "Could not find the OpenSSL install library directory."
  exit 1
fi
LDFLAGS=${LDFLAGS:-"-Wl,-rpath,${OPENSSL_LIB}"}

# Then nghttp3
echo "Building nghttp3..."
[ ! -d nghttp3 ] && git clone --depth 1 -b v0.13.0 https://github.com/ngtcp2/nghttp3.git
cd nghttp3
autoreconf -if
./configure \
  --prefix=${BASE} \
  PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${OPENSSL_LIB}/pkgconfig \
  CFLAGS="${CFLAGS}" \
  CXXFLAGS="${CXXFLAGS}" \
  LDFLAGS="${LDFLAGS}" \
  --enable-lib-only
${MAKE} -j ${num_threads}
sudo ${MAKE} install
sudo chmod -R a+rX ${BASE}
cd ..

# Now ngtcp2
echo "Building ngtcp2..."
[ ! -d ngtcp2 ] && git clone --depth 1 -b v0.17.0 https://github.com/ngtcp2/ngtcp2.git
cd ngtcp2
autoreconf -if
./configure \
  --prefix=${BASE} \
  PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${OPENSSL_LIB}/pkgconfig \
  CFLAGS="${CFLAGS}" \
  CXXFLAGS="${CXXFLAGS}" \
  LDFLAGS="${LDFLAGS}" \
  --enable-lib-only
${MAKE} -j ${num_threads}
sudo ${MAKE} install
sudo chmod -R a+rX ${BASE}
cd ..

# Then nghttp2, with support for H3
echo "Building nghttp2 ..."
[ ! -d nghttp2 ] && git clone --depth 1 -b v1.55.1 https://github.com/tatsuhiro-t/nghttp2.git
cd nghttp2
autoreconf -if
if [ `uname -s` = "Darwin" ] || [ `uname -s` = "FreeBSD" ]
then
  # --enable-app requires systemd which is not available on Mac/FreeBSD.
  ENABLE_APP=""
else
  ENABLE_APP="--enable-app"
fi

# Note for FreeBSD: This will not build h2load. h2load can be run on a remote machine.
./configure \
  --prefix=${BASE} \
  PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${OPENSSL_LIB}/pkgconfig \
  CFLAGS="${CFLAGS}" \
  CXXFLAGS="${CXXFLAGS}" \
  LDFLAGS="${LDFLAGS} -L${OPENSSL_LIB}" \
  --enable-http3 \
  ${ENABLE_APP}
${MAKE} -j ${num_threads}
sudo ${MAKE} install
sudo chmod -R a+rX ${BASE}
cd ..

# Then curl
echo "Building curl ..."
[ ! -d curl ] && git clone --depth 1 -b curl-8_2_1 https://github.com/curl/curl.git
cd curl
# On mac autoreconf fails on the first attempt with an issue finding ltmain.sh.
# The second runs fine.
autoreconf -fi || autoreconf -fi
./configure \
  --prefix=${BASE} \
  --with-ssl=${OPENSSL_PREFIX} \
  --with-nghttp2=${BASE} \
  --with-nghttp3=${BASE} \
  --with-ngtcp2=${BASE} \
  CFLAGS="${CFLAGS}" \
  CXXFLAGS="${CXXFLAGS}" \
  LDFLAGS="${LDFLAGS}"
${MAKE} -j ${num_threads}
sudo ${MAKE} install
sudo chmod -R a+rX ${BASE}
cd ..
