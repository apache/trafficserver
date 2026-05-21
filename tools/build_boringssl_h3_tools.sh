#!/usr/bin/env bash
#
#  Simple script to build BoringsSSL and various tools with H3 and QUIC support
#  including quiche+BoringSSL.
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


# Set these, if desired, to change these to your preferred installation
# directory
BASE=${BASE:-"/opt/h3-tools-boringssl"}
MAKE="make"

echo "Building boringssl H3 dependencies in ${WORKDIR}. Installation will be done in ${BASE}"

CFLAGS=${CFLAGS:-"-O3 -g"}
CXXFLAGS=${CXXFLAGS:-"-O3 -g"}
BORINGSSL_PATH="${BASE}/boringssl"
GO_VERSION=${GO_VERSION:-"1.26.2"}
BORINGSSL_COMMIT=${BORINGSSL_COMMIT:-"c3ffc3300a9450cf8e396c7880be7c6cadc16a4a"}
QUICHE_TAG=${QUICHE_TAG:-"0.28.0"}
CURL_TAG=${CURL_TAG:-"curl-8_20_0"}
NGHTTP3_TAG=${NGHTTP3_TAG:-"v1.15.0"}
NGTCP2_TAG=${NGTCP2_TAG:-"v1.22.1"}
NGHTTP2_TAG=${NGHTTP2_TAG:-"v1.69.0"}

if [ -e /etc/redhat-release ]; then
    MAKE="gmake"
    TMP_BORINGSSL_LIB_PATH="${BASE}/boringssl/lib64"
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
    TMP_BORINGSSL_LIB_PATH="${BASE}/boringssl/lib"
    echo "+-------------------------------------------------------------------------+"
    echo "| You probably need to run this, or something like this, for your system: |"
    echo "|                                                                         |"
    echo "|   sudo apt -y install libev-dev libjemalloc-dev python3-dev libxml2-dev |"
    echo "|   sudo apt -y install libpython3-dev libc-ares-dev libsystemd-dev       |"
    echo "|   sudo apt -y install libevent-dev libjansson-dev zlib1g-dev libpsl-dev |"
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

if [ -z ${BORINGSSL_LIB_PATH+x} ]; then
   BORINGSSL_LIB_PATH=${TMP_BORINGSSL_LIB_PATH:-"${BORINGSSL_PATH}/lib"}
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

wget https://go.dev/dl/go${GO_VERSION}.${OS}-${ARCH}.tar.gz
rm -rf ${BASE}/go && tar -C ${BASE} -xf go${GO_VERSION}.${OS}-${ARCH}.tar.gz
rm go${GO_VERSION}.${OS}-${ARCH}.tar.gz

GO_BINARY_PATH=${BASE}/go/bin/go
if [ ! -d boringssl ]; then
  git clone https://boringssl.googlesource.com/boringssl
  cd boringssl
  git checkout ${BORINGSSL_COMMIT}
  cd ..
fi
cd boringssl

# un-set it for a bit.
set +e
BSSL_C_FLAGS="-Wdangling-pointer=0"
GCCO=$(eval "gcc --help=warnings | grep dangling-pointer=")
retVal=$?
if [ $retVal -eq 1 ]; then
    BSSL_C_FLAGS=""
fi
set -e

# Check compiler flags before passing them to CMake. GCC errors on some
# Clang-only -Wno-error flags, including -Wno-error=character-conversion.
compiler_supports_flag() {
  local compiler=$1
  local flag=$2

  echo '' | "${compiler}" "${flag}" -x c++ -c -o /dev/null - >/dev/null 2>&1
}

# Note: -Wdangling-pointer=0
#   We may have some issues with latest GCC compilers, so disabling -Wdangling-pointer=
# Note: -UBORINGSSL_HAVE_LIBUNWIND
#   Disable related libunwind test builds, there are some version number issues
#   with this pkg in Ubuntu 20.04, so disable this to make sure it builds.
BSSL_CXX_FLAGS="-Wno-error=ignored-attributes -UBORINGSSL_HAVE_LIBUNWIND"
if compiler_supports_flag c++ -Wno-error=character-conversion; then
  BSSL_CXX_FLAGS="-Wno-error=character-conversion ${BSSL_CXX_FLAGS}"
fi

cmake \
  -B build-shared \
  -DGO_EXECUTABLE=${GO_BINARY_PATH} \
  -DCMAKE_INSTALL_PREFIX=${BASE}/boringssl \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="${BSSL_CXX_FLAGS}" \
  -DCMAKE_C_FLAGS=${BSSL_C_FLAGS} \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_TESTING=0 \
  -DCMAKE_THREAD_LIBS_INIT="-lpthread" \
  -DTHREADS_PREFER_PTHREAD_FLAG=ON \
  -DBUILD_SHARED_LIBS=1
cmake \
  -B build-static \
  -DGO_EXECUTABLE=${GO_BINARY_PATH} \
  -DCMAKE_INSTALL_PREFIX=${BASE}/boringssl \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="${BSSL_CXX_FLAGS}" \
  -DCMAKE_C_FLAGS="${BSSL_C_FLAGS}" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_TESTING=0 \
  -DCMAKE_THREAD_LIBS_INIT="-lpthread" \
  -DTHREADS_PREFER_PTHREAD_FLAG=ON \
  -DBUILD_SHARED_LIBS=0
cmake --build build-shared -j ${num_threads}
cmake --build build-static -j ${num_threads}
sudo cmake --install build-shared
sudo cmake --install build-static
sudo chmod -R a+rX ${BASE}

cd ..

# Build quiche
# Steps borrowed from: https://github.com/apache/trafficserver-ci/blob/main/docker/rockylinux8/Dockerfile
echo "Building quiche"
QUICHE_BASE="${BASE:-/opt}/quiche"
[ ! -d quiche ] && git clone  https://github.com/cloudflare/quiche.git
cd quiche
git checkout ${QUICHE_TAG}
QUICHE_BSSL_PATH=${BORINGSSL_LIB_PATH} QUICHE_BSSL_LINK_KIND=dylib cargo build -j4 --package quiche --release --features ffi,pkg-config-meta,qlog
sudo mkdir -p ${QUICHE_BASE}/lib/pkgconfig
sudo mkdir -p ${QUICHE_BASE}/include
sudo cp target/release/libquiche.a ${QUICHE_BASE}/lib/
if [ -f target/release/libquiche.so ]; then
  sudo cp target/release/libquiche.so ${QUICHE_BASE}/lib/
  # Why a link? https://github.com/cloudflare/quiche/issues/1808#issuecomment-2196233378
  sudo ln -sf ${QUICHE_BASE}/lib/libquiche.so ${QUICHE_BASE}/lib/libquiche.so.0
fi
sudo cp quiche/include/quiche.h ${QUICHE_BASE}/include/
sudo cp target/release/quiche.pc ${QUICHE_BASE}/lib/pkgconfig
sudo chmod -R a+rX ${BASE}
cd ..

LDFLAGS=${LDFLAGS:-"-Wl,-rpath,${BORINGSSL_LIB_PATH}"}

# Then nghttp3
echo "Building nghttp3..."
[ ! -d nghttp3 ] && git clone --depth 1 -b ${NGHTTP3_TAG} https://github.com/ngtcp2/nghttp3.git
cd nghttp3
git submodule update --init
autoreconf -if
./configure \
  --prefix=${BASE} \
  PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${BORINGSSL_LIB_PATH}/pkgconfig \
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
[ ! -d ngtcp2 ] && git clone --depth 1 -b ${NGTCP2_TAG} https://github.com/ngtcp2/ngtcp2.git
cd ngtcp2
git submodule update --init
autoreconf -if
./configure \
  --prefix=${BASE} \
  --with-boringssl \
  BORINGSSL_CFLAGS="-I${BORINGSSL_PATH}/include" \
  BORINGSSL_LIBS="-L${BORINGSSL_LIB_PATH} -lssl -lcrypto" \
  PKG_CONFIG_PATH=${BASE}/lib/pkgconfig \
  CFLAGS="${CFLAGS} -fPIC" \
  CXXFLAGS="${CXXFLAGS} -fPIC" \
  LDFLAGS="${LDFLAGS}" \
  --enable-lib-only
${MAKE} -j ${num_threads}
sudo ${MAKE} install
sudo chmod -R a+rX ${BASE}
cd ..

# Then nghttp2, with support for H3
echo "Building nghttp2 ..."
[ ! -d nghttp2 ] && git clone --depth 1 -b ${NGHTTP2_TAG} https://github.com/nghttp2/nghttp2.git
cd nghttp2
git submodule update --init
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
  PKG_CONFIG_PATH=${BASE}/lib/pkgconfig \
  CFLAGS="${CFLAGS} -I${BORINGSSL_PATH}/include" \
  CXXFLAGS="${CXXFLAGS} -I${BORINGSSL_PATH}/include" \
  LDFLAGS="${LDFLAGS}" \
  OPENSSL_LIBS="-lcrypto -lssl -L${BORINGSSL_LIB_PATH}" \
  --enable-http3 \
  --disable-examples \
  ${ENABLE_APP}
${MAKE} -j ${num_threads}
sudo ${MAKE} install
sudo chmod -R a+rX ${BASE}
cd ..

# Then curl
echo "Building curl ..."
[ ! -d curl ] && git clone --depth 1 -b ${CURL_TAG} https://github.com/curl/curl.git
cd curl
# On mac autoreconf fails on the first attempt with an issue finding ltmain.sh.
# The second runs fine.
autoreconf -fi || autoreconf -fi
# Keep discovery on PKG_CONFIG_PATH so curl finds ngtcp2 and its BoringSSL crypto backend together.
PKG_CONFIG_PATH=${BASE}/lib/pkgconfig:${BORINGSSL_LIB_PATH}/pkgconfig \
./configure \
  --prefix=${BASE} \
  --with-openssl="${BORINGSSL_PATH}" \
  --with-nghttp2 \
  --with-nghttp3 \
  --with-ngtcp2 \
  LDFLAGS="${LDFLAGS} -L${BORINGSSL_LIB_PATH} -Wl,-rpath,${BORINGSSL_LIB_PATH}" \
  CFLAGS="${CFLAGS}" \
  CXXFLAGS="${CXXFLAGS}"
${MAKE} -j ${num_threads}
sudo ${MAKE} install
sudo chmod -R a+rX ${BASE}
cd ..
