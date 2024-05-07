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


# The whole idea is to end up with two set of tools, a borinssgl toolset and an
# openssl one. The first one can be used to build ATS+Boringssl+quiche(borinssl) while the
# later one will give the base to build ATS on top of openssl/quictls+quiche(openssl/quictls).


SCRIPT_PATH=$(dirname $0)
# We make boringssl tools first.
BASE=${BASE:-"/opt"}/h3-tools-boringssl ${SCRIPT_PATH}/build_boringssl_h3_tools.sh
if [ $? -ne 0 ]; then
    echo "build_boringssl_h3_tools script Failed."
    exit 1
fi

# then openssl/quictls.
BASE=${BASE:-"/opt"}/h3-tools-openssl ${SCRIPT_PATH}/build_openssl_h3_tools.sh
if [ $? -ne 0 ]; then
    echo "build_openssl_h3_tools script Failed."
    exit 1
fi
