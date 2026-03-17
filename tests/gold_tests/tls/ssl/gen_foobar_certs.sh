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

# bash script used to generate sni parent test certs

for name in foo bar
do
  openssl req -x509 -newkey rsa:2048 \
    -keyout server-${name}.key -out server-${name}.pem \
    -days 3650 -nodes \
    -subj "/C=US/ST=CO/L=Denver/O=Comcast/OU=Edge/CN=${name}.com" \
    -addext "subjectAltName=DNS:${name}.com,DNS:www.${name}.com,DNS:*.${name}.com" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -addext "subjectKeyIdentifier=hash"
done
