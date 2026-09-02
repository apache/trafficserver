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
FROM ubuntu:resolute AS build-setup

ARG LLVM_VERSION=22
ARG BASE=/opt

RUN apt update \
 && apt upgrade --yes \
 && apt install --no-install-recommends --yes \
    ca-certificates \
    clang-${LLVM_VERSION} \
    clang-tools-${LLVM_VERSION} \
    lld-${LLVM_VERSION} \
    cmake \
    ninja-build \
    libc-ares-dev \
    libsystemd-dev \
    libev-dev \
    libevent-dev \
    zlib1g-dev \
    wget \
    git \
    libtool \
    make \
    pkg-config \
    libpsl-dev \
    libssl-dev \
# ATS deps
    libxml2-dev \
    libjemalloc-dev \
    libhwloc-dev \
    libfmt-dev \
    libpcre2-dev \
    hwloc \
    libbrotli-dev \
    libzstd-dev \
    luajit \
    libluajit-5.1-dev \
    libcap-dev \
    libmagick++-dev \
    libmaxminddb-dev \
    libcjose-dev \
    libcjose0 \
    libjansson-dev \
 && apt clean --yes

# Set up cc (and optionally c++) to use clang-18 via alternatives
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/clang-${LLVM_VERSION} 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-${LLVM_VERSION} 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${LLVM_VERSION} 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${LLVM_VERSION} 100

FROM build-setup as build

ARG ATS_VERSION=10.2.0

RUN git clone --depth 1 -b ${ATS_VERSION} https://github.com/apache/trafficserver.git \
 && cmake \
     -Strafficserver \
     -Bbuild \
     -GNinja \
     -DBUILD_SHARED_LIBS=ON \
     -DCMAKE_C_COMPILER=clang-22 \
     -DCMAKE_CXX_COMPILER=clang++-22 \
     -DCMAKE_LINKER_TYPE=LLD \
     -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
     -DCMAKE_INSTALL_PREFIX=${BASE} \
     -DCMAKE_BUILD_TYPE=Release \
     -DBUILD_TESTING=OFF \
     -DBUILD_REGRESSION_TESTING=OFF \
     -DENABLE_AUTEST=OFF \
     -DBUILD_EXPERIMENTAL_PLUGINS=ON \
     -DENABLE_JEMALLOC=ON \
     -DENABLE_MALLOC_ALLOCATOR=ON \
     -DENABLE_CRIPTS=ON \
     -DENABLE_EXAMPLE=OFF \
     -DENABLE_LUAJIT=ON \
 && cmake --build build \
 && cmake --install build \
 && rm -rf build trafficserver

FROM ubuntu:resolute

RUN apt update \
 && apt upgrade --yes \
 && apt install --no-install-recommends --yes \
    ca-certificates \
    libjemalloc2 \
    libxml2-16 \
    hwloc \
    libmaxminddb0 \
    libfmt10 \
    libpcre2-8-0 \
    libbrotli1 \
    luajit \
    libcap2 \
    libevent-2.1-7t64 \
    libev4t64 \
    libcares2 \
    libmagick++-7.q16-5 \
    libmagickcore-7.q16-10 \
    libmagickwand-7.q16-10 \
    libcjose0 \
    libjansson4 \
 && apt clean --yes

COPY --from=build /opt /opt
RUN chown nobody /opt/var/trafficserver /opt/var/log/trafficserver
ENV PATH="$PATH:/opt/bin"

EXPOSE 8080

CMD ["traffic_server"]
