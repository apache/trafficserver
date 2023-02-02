#  Dockerfile to build http3 tools for testing
#
#  You can build this image like so from the top source dir:
#
#  mkdir context
#  docker build -t h3tools -f tools/Dockerfile-h3tools context
#
#  And then run some tools:
#
#  docker run -it --rm h3tools curl -k --http3 https://172.17.0.1:443/
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

FROM ubuntu:22.04 AS build

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive \
    TZ=US/Central \
    apt-get -y install gnupg2 ca-certificates tzdata

RUN apt-get -y install \
    build-essential \
    autoconf \
    automake \
    libtool \
    pkg-config \
    git \
    libev-dev \
    libc-ares-dev \
    libxml2-dev \
    libz-dev \
    && apt-get clean

ENV PREFIX=/opt/h3tools
ENV PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig
ENV LDFLAGS="-Wl,-rpath,$PREFIX/lib -Wl,-rpath,$PREFIX/lib64 -L$PREFIX/lib"
ENV CFLAGS="-O3 -DNDEBUG"
ENV CXXFLAGS="-O3 -DNDEBUG"

RUN git clone https://github.com/jemalloc/jemalloc.git && \
    cd jemalloc && \
    ./autogen.sh && \
    ./configure --prefix=$PREFIX && \
    make install && \
    cd ..

RUN git clone https://github.com/ngtcp2/nghttp3.git && \
    cd nghttp3 && \
    autoreconf -if && \
    ./configure --prefix=$PREFIX --enable-lib-only && \
    make -j20 install && \
    cd ..

RUN git clone https://github.com/quictls/openssl.git && \
    cd openssl && \
    ./config enable-tls1_3 --prefix=$PREFIX && \
    make -j20 && \
    make install_sw && \
    cd ..

RUN git clone https://github.com/ngtcp2/ngtcp2.git && \
    cd ngtcp2 && \
    autoreconf -if && \
    ./configure --prefix=$PREFIX --with-jemalloc && \
    make -j20 install && \
    cd ..

RUN git clone https://github.com/nghttp2/nghttp2.git && \
    cd nghttp2 && \
    autoreconf -if && \
    ./configure --with-jemalloc=$PREFIX --enable-http3 --with-libngtcp2 --with-libnghttp3 --prefix=$PREFIX && \
    make -j20 && \
    make install && \
    cd ..

RUN git clone https://github.com/curl/curl.git && \
    cd curl && \
    autoreconf -if && \
    ./configure --with-ssl=$PREFIX --with-ngtcp2=$PREFIX --with-nghttp3=$PREFIX --with-nghttp2=$PREFIX --prefix=$PREFIX && \
    make -j20 && \
    make install && \
    cd ..


FROM ubuntu:22.04

RUN apt-get update && apt-get -y install libev4 ca-certificates && apt-get clean

COPY --from=build /opt/h3tools /opt/h3tools
ENV PATH "$PATH:/opt/h3tools/bin"

