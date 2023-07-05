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


# See https://github.com/apache/trafficserver/issues/9880
ignore_unexpecte_eof=''
if openssl s_client --help 2>&1 | grep -q ignore_unexpected_eof
then
  ignore_unexpected_eof='-ignore_unexpected_eof'
fi
nc -l -p "$1" -c 'echo -e "This is a reply"' -o test.out &
echo "This is a test" | openssl s_client -servername bar.com -connect "localhost:$2" -ign_eof ${ignore_unexpected_eof} "${@:3}"
