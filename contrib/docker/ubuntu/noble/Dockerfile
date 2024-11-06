#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################
FROM ubuntu:noble AS build

ARG LLVM_VERSION=18
ARG BASE=/opt
ARG ATS_VERSION=10.0.0

RUN apt update \
 && apt upgrade --yes \
 && apt install --no-install-recommends --yes \
    ca-certificates \
    clang-${LLVM_VERSION} \
    libc++-${LLVM_VERSION}-dev \
    cmake \
    ninja-build \
    libc-ares-dev \
    libsystemd-dev \
    libev-dev \
    libevent-dev \
    zlib1g-dev \
    cargo \
    wget \
    git \
    libtool \
    make \
    pkg-config \
# ATS deps
    libxml2-dev \
    libjemalloc-dev \
    libhwloc-dev \
    libfmt-dev \
    libpcre2-dev \
    libpcre3-dev \
    hwloc \
    libbrotli-dev \
    luajit \
    libcap-dev \
    libmagick++-dev \
    libmaxminddb-dev \
    libcjose-dev \
    libcjose0 \
    libjansson-dev \
 # needed to appease nghttp2 but unused
    libssl-dev \
 && apt clean --yes

RUN mkdir -p ${BASE} && chmod a+rX ${BASE}

RUN if [ `uname -m` = "arm64" -o `uname -m` = "aarch64" ]; then echo "arm64" > /arch; else echo "amd64" > /arch; fi \
  && wget -qO- https://go.dev/dl/go1.21.6.linux-$(cat /arch).tar.gz | tar -C ${BASE} -xzf -

ENV CC=clang-${LLVM_VERSION}
ENV CXX=clang++-${LLVM_VERSION}
ENV GO_BINARY_PATH=${BASE}/go/bin/go

RUN git clone https://boringssl.googlesource.com/boringssl \
 && cd boringssl \
 && git checkout 45b2464158379f48cec6e35a1ef503ddea1511a6 \
 && cmake \
  -B build-shared \
  -G Ninja \
  -DGO_EXECUTABLE=${GO_BINARY_PATH} \
  -DCMAKE_INSTALL_PREFIX=${BASE}/boringssl \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS='-Wno-error=ignored-attributes -UBORINGSSL_HAVE_LIBUNWIND' \
  -DCMAKE_C_FLAGS=${BSSL_C_FLAGS} \
  -DBUILD_SHARED_LIBS=1 \
 && cmake \
  -B build-static \
  -G Ninja \
  -DGO_EXECUTABLE=${GO_BINARY_PATH} \
  -DCMAKE_INSTALL_PREFIX=${BASE}/boringssl \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS='-Wno-error=ignored-attributes -UBORINGSSL_HAVE_LIBUNWIND' \
  -DCMAKE_C_FLAGS="${BSSL_C_FLAGS}" \
  -DBUILD_SHARED_LIBS=0 \
 && cmake --build build-shared  \
 && cmake --build build-static  \
 && cmake --install build-shared \
 && cmake --install build-static \
 && cd .. \
 && rm -rf boringssl

ENV QUICHE_BASE="${BASE}/quiche"

RUN git clone -b 0.22.0 --depth 1 https://github.com/cloudflare/quiche.git \
 && cd quiche \
 && QUICHE_BSSL_PATH=${BASE}/boringssl/lib QUICHE_BSSL_LINK_KIND=dylib \
    cargo build -j$(nproc) --package quiche --release --features ffi,pkg-config-meta,qlog \
 && mkdir -p ${QUICHE_BASE}/lib/pkgconfig \
 && mkdir -p ${QUICHE_BASE}/include \
 && cp target/release/libquiche.a ${QUICHE_BASE}/lib/ \
 && cp target/release/libquiche.so ${QUICHE_BASE}/lib/ \
 && ln -s ${QUICHE_BASE}/lib/libquiche.so ${QUICHE_BASE}/lib/libquiche.so.0 \
 && cp quiche/include/quiche.h ${QUICHE_BASE}/include/ \
 && cp target/release/quiche.pc ${QUICHE_BASE}/lib/pkgconfig \
 && cd .. \
 && rm -rf quiche

ENV LDFLAGS="-Wl,-rpath,${BASE}/boringssl/lib"
ENV CFLAGS="-O3"
ENV CXXFLAGS="-O3"
ENV PKG_CONFIG_PATH="${BASE}/lib/pkgconfig:${BASE}/boringssl/lib/pkgconfig:${BASE}/quiche/lib/pkgconfig"

RUN git clone --depth 1 -b v1.2.0 https://github.com/ngtcp2/nghttp3.git \
 && cd nghttp3 \
 && git submodule update --init \
 && autoreconf -if \
 && ./configure \
  --prefix=${BASE} \
  CFLAGS="${CFLAGS}" \
  CXXFLAGS="${CXXFLAGS}" \
  LDFLAGS="${LDFLAGS}" \
  --enable-lib-only \
 && make -j $(nproc) \
 && make install \
 && cd .. \
 && rm -rf nghttp3


RUN git clone --depth 1 -b v1.4.0 https://github.com/ngtcp2/ngtcp2.git \
 && cd ngtcp2 \
 && autoreconf -if \
 && ./configure \
  --prefix=${BASE} \
  --with-boringssl \
  BORINGSSL_CFLAGS="-I${BASE}/boringssl/include" \
  BORINGSSL_LIBS="-L${BASE}/boringssl/lib -lssl -lcrypto" \
  CFLAGS="${CFLAGS} -fPIC" \
  CXXFLAGS="${CXXFLAGS} -fPIC" \
  LDFLAGS="${LDFLAGS}" \
  --enable-lib-only \
 && make -j $(nproc) \
 && make install \
 && cd .. \
 && rm -rf ngtcp2

RUN git clone --depth 1 -b v1.60.0 https://github.com/tatsuhiro-t/nghttp2.git \
 && cd nghttp2 \
 && git submodule update --init \
 && autoreconf -if \
 && ./configure \
  --prefix=${BASE} \
  CFLAGS="${CFLAGS} -I${BASE}/boringssl/include" \
  CXXFLAGS="${CXXFLAGS} -I${BASE}/boringssl/include" \
  LDFLAGS="${LDFLAGS}" \
  OPENSSL_LIBS="-L${BASE}/boringssl/lib -lcrypto -lssl" \
  --enable-http3 \
  --disable-examples \
  --enable-app \
 && make -j $(nproc) \
 && make install \
 && cd .. \
 && rm -rf nghttp2

RUN git clone --depth 1 -b curl-8_7_1 https://github.com/curl/curl.git \
 && cd curl \
 && autoreconf -fi \
 && ./configure \
  --prefix=${BASE} \
  --with-openssl="${BASE}/boringssl" \
  --with-nghttp2=${BASE} \
  --with-nghttp3=${BASE} \
  --with-ngtcp2=${BASE} \
  LDFLAGS="${LDFLAGS} -L${BASE}/boringssl/lib -Wl,-rpath,${BASE}/boringssl/lib" \
  CFLAGS="${CFLAGS}" \
  CXXFLAGS="${CXXFLAGS}" \
 && make -j $(nproc) \
 && make install \
 && cd .. \
 && rm -rf curl

RUN git clone --depth 1 -b ${ATS_VERSION} https://github.com/apache/trafficserver.git \
 && cmake \
     -Strafficserver \
     -Bbuild \
     -GNinja \
     -DCMAKE_INSTALL_PREFIX=${BASE} \
     -DCMAKE_BUILD_TYPE=Release \
     -DBUILD_TESTING=OFF \
     -DBUILD_REGRESSION_TESTING=OFF \
     -DENABLE_AUTEST=OFF \
     -DBUILD_EXPERIMENTAL_PLUGINS=ON \
     -DENABLE_JEMALLOC=ON \
     -DENABLE_MALLOC_ALLOCATOR=ON \
     -DENABLE_QUICHE=ON \
     -DENABLE_CRIPTS=ON \
     -DENABLE_EXAMPLE=OFF \
     -DOPENSSL_ROOT_DIR=${BASE}/boringssl \
     -Dquiche_ROOT=${QUICHE_BASE} \
 && cmake --build build \
 && cmake --install build \
 && rm -rf build trafficserver

FROM ubuntu:noble

RUN apt update \
 && apt upgrade --yes \
 && apt install --no-install-recommends --yes \
    ca-certificates \
    libjemalloc2 \
    libxml2 \
    hwloc \
    libmaxminddb0 \
    libfmt9 \
    libpcre2-8-0 \
    libpcre3 \
    libbrotli1 \
    luajit \
    libcap2 \
    libevent-2.1-7t64 \
    libev4t64 \
    libcares2 \
    libmagick++-6.q16-9t64 \
    libmagickcore-6.q16-7t64 \
    libmagickwand-6.q16-7t64 \
    libcjose0 \
    libjansson4 \
 && apt clean --yes

COPY --from=build /opt /opt
RUN chown nobody /opt/var/trafficserver /opt/var/log/trafficserver
ENV PATH="$PATH:/opt/bin"

EXPOSE 8080

CMD ["traffic_server"]
