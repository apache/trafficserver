#!/bin/bash

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
set -o pipefail

case ${1} in
trusty*|jessie*)
    sed -i 's/^mesg n$/tty -s \&\& mesg n/g' /root/.profile
    apt-get update
    apt-get install -y \
        autoconf \
        automake \
        build-essential \
        clang \
        libcap-dev \
        libcurl4-openssl-dev \
        libhwloc-dev \
        liblua5.2-dev \
        libpcre3-dev \
        libssl-dev \
        m4 \
        ncurses-dev \
        git \
        tcl-dev
;;

centos*)
    yum install -y \
        autoconf \
        automake \
        clang \
        flex \
        gcc \
        gcc-c++ \
        hwloc-devel \
        libcap-devel \
        libcurl-devel d\
        libtool \
        lua-devel \
        m4 \
        ncurses-devel \
        openssl-devel \
        pcre-devel \
        git \
        tcl-devel
;;

fedora*)
    dnf install -y \
        autoconf \
        automake \
        clang \
        flex \
        gcc \
        gcc-c++ \
        hwloc-devel \
        libcap-devel \
        libcurl-devel \
        libtool \
        lua-devel \
        m4 \
        ncurses-devel \
        openssl-devel \
        pcre-devel \
        tcl-devel \
        git \
        make
;;

omnios)
    export PATH=/usr/gnu/bin:/usr/bin:/usr/sbin:/sbin:/opt/gcc-4.8.1/bin
    echo "export PATH=/usr/gnu/bin:/usr/bin:/usr/sbin:/sbin:/opt/gcc-4.8.1/bin" >> /root/.profile
    RC=0
    if [[ ! $(grep http://pkg.omniti.com/omniti-ms/ /var/pkg/pkg5.image) ]]; then
        pkg set-publisher -g http://pkg.omniti.com/omniti-ms/ ms.omniti.com
    fi
    pkg refresh
    pkg install \
        archiver/gnu-tar \
        developer/build/autoconf \
        developer/build/automake \
        developer/build/gnu-make \
        developer/build/libtool \
        developer/gcc48 \
        developer/lexer/flex \
        developer/library/lint \
        developer/linker \
        developer/object-file \
        developer/parser/bison \
        developer/versioning/git \
        library/idnkit \
        library/idnkit/header-idnkit \
        omniti/runtime/tcl-8 \
        omniti/system/hwloc \
        system/header \
        system/library/math \
        || RC=${?}
    if [[ ${RC} != 0 ]] && [[ ${RC} != 4 ]]; then
        exit 1
    fi
;;

*)
    echo "no config for ${1}"
    exit 1
;;
esac

exit 0
