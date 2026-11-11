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

# Bash script to generate certificates for tls_client_verify3 Au test using the openssl command.

let N=1
for PREFIX in aaa bbb ccc
do
  rm -f ${PREFIX}-ca.key
  openssl genrsa -passout pass:12345678 -des3 -out ${PREFIX}-ca.key 2048

  rm -f ${PREFIX}-ca.pem
  openssl req -passin pass:12345678 -config ./openssl.cnf -x509 -new -nodes -subj /CN=${PREFIX}-ca \
    -key ${PREFIX}-ca.key -sha256 -days 36500 -batch -out ${PREFIX}-ca.pem

  rm -f ${PREFIX}-signed.key
  openssl genrsa -passout pass:12345678 -out ${PREFIX}-signed.key 2048

  rm -f tmp.csr
  openssl req -passin pass:12345678 -config ./openssl.cnf -subj /CN=${PREFIX}-signed -new \
    -key ${PREFIX}-signed.key -batch -out tmp.csr

  rm -f ${PREFIX}-signed.pem
  openssl x509 -passin pass:12345678 -req -in tmp.csr -CA ${PREFIX}-ca.pem -CAkey ${PREFIX}-ca.key \
    -set_serial 0$N -out ${PREFIX}-signed.pem -days 36500 -sha256

  rm -f tmp.csr ${PREFIX}-ca.srl

  let N=N+1
done
