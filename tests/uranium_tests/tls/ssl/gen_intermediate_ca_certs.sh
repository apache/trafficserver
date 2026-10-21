#!/usr/bin/env bash
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

# Generate a three-level client PKI for the tls_resume_intermediate_ca Au test:
#
#   intca-root (self-signed root CA)
#     -> intca-sub (intermediate CA)
#          -> intclient (client leaf certificate)
#
# The test configures ATS to trust only intca-root and has the client present
# intclient together with intca-sub, so the intermediate must be supplied by the
# client during the handshake rather than by the server's trust store.

set -euo pipefail

cd "$(dirname "$0")"

rm -f intca-root.key intca-root.pem intca-sub.key intca-sub.pem intclient.key intclient.pem

openssl genrsa -out intca-root.key 2048
openssl req -config ./openssl.cnf -x509 -new -nodes -subj /CN=intca-root \
  -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,keyCertSign,cRLSign" \
  -key intca-root.key -sha256 -days 36500 -batch -out intca-root.pem

openssl genrsa -out intca-sub.key 2048
openssl req -config ./openssl.cnf -new -subj /CN=intca-sub -key intca-sub.key -batch -out tmp.csr
openssl x509 -req -in tmp.csr -CA intca-root.pem -CAkey intca-root.key -set_serial 0x10 \
  -days 36500 -sha256 -out intca-sub.pem \
  -extfile <(printf 'basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n')

openssl genrsa -out intclient.key 2048
openssl req -config ./openssl.cnf -new -subj /CN=intclient -key intclient.key -batch -out tmp.csr
openssl x509 -req -in tmp.csr -CA intca-sub.pem -CAkey intca-sub.key -set_serial 0x11 \
  -days 36500 -sha256 -out intclient.pem \
  -extfile <(printf 'basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\n')

rm -f tmp.csr

openssl verify -CAfile intca-root.pem -untrusted intca-sub.pem intclient.pem
