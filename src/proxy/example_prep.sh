#!/bin/sh

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#

echo "[example_prep.sh] Checking/Moving old cores...";

HOST=`uname -n`
export HOST
NOW=`date | tr ' ' '-'`

WHERE="$ROOT"
[ -z "$WHERE" ] && WHERE="$INST_ROOT"
[ -z "$WHERE" ] && WHERE=`head -1 /etc/traffic_server 2>/dev/null`
[ -z "$WHERE" ] && WHERE="/home/trafficserver"

[ -f core ] && mv core ${WHERE}/logs/traffic_server.core.$HOST.$NOW
