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
################################################################################

# install build stuff
CFLAGS_SAVE="$CFLAGS"
CXXFLAGS_SAVE="$CXXFLAGS"
RUSTFLAGS_SAVE="$RUSTFLAGS"
unset CFLAGS
unset CXXFLAGS
unset RUSTFLAGS
export AFL_NOOPT=1

apt-get install -y libev-dev libjemalloc-dev python2-dev libxml2-dev libpython2-dev libc-ares-dev libsystemd-dev libevent-dev libjansson-dev zlib1g-dev sudo autoconf libtool pkg-config
curl https://sh.rustup.rs -sSf | sh -s -- -y --default-toolchain=nightly
export PATH="/root/.cargo/bin:${PATH}"

# Build tools folder will be /opt/h3-tools-boringssl
BASE=/opt $SRC/trafficserver/tools/build_h3_tools.sh

export CFLAGS="${CFLAGS_SAVE}"
export CXXFLAGS="${CXXFLAGS_SAVE}"
export RUSTFLAGS="${RUSTFLAGS_SAVE}"
unset AFL_NOOPT

# don't use __cxa_atexit for coverage sanitizer
if [[ $SANITIZER = coverage ]]
then
    export CXXFLAGS="$CXXFLAGS -fno-use-cxa-atexit"
fi

# don't use unsigned-integer-overflow sanitizer {Bug in system include files}
if [[ $SANITIZER = undefined ]]
then
    export CXXFLAGS="$CXXFLAGS -fno-sanitize=unsigned-integer-overflow"
fi

mkdir -p build && cd build/
cmake -DENABLE_POSIX_CAP=OFF -DENABLE_FUZZING=ON -DYAML_BUILD_SHARED_LIBS=OFF -DENABLE_HWLOC=OFF -DENABLE_JEMALLOC=OFF -DENABLE_LUAJIT=OFF -Dquiche_ROOT=/opt/h3-tools-boringssl/quiche -DENABLE_QUICHE=TRUE -DOPENSSL_INCLUDE_DIR=/opt/h3-tools-boringssl/boringssl/include -DOPENSSL_ROOT_DIR=/opt/h3-tools-boringssl/boringssl ../.
make -j$(nproc) --ignore-errors

cp tests/fuzzing/fuzz_* $OUT/
cp -r tests/fuzzing/lib/ $OUT/
cp $SRC/trafficserver/tests/fuzzing/*.zip  $OUT/

cp /opt/h3-tools-boringssl/boringssl/lib/libssl.so $OUT/lib/
cp /opt/h3-tools-boringssl/boringssl/lib/libcrypto.so $OUT/lib/
cp /opt/h3-tools-boringssl/quiche/lib/libquiche.so $OUT/lib/

ln -s $OUT/lib/libquiche.so $OUT/lib/libquiche.so.0
export LD_LIBRARY_PATH=$OUT/lib/
ldconfig

if [[ $SANITIZER = undefined ]]
then
    rm -f $OUT/fuzz_http
    rm -f $OUT/fuzz_hpack
    rm -f $OUT/fuzz_http3frame
fi
