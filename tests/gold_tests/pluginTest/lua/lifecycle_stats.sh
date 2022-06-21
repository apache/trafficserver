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

traffic_ctl plugin msg ts_lua print_stats
N=60
while (( N > 0 ))
do
    sleep 1
    rm -f lifecycle.out
    grep -F ' ts_lua ' ${PROXY_CONFIG_LOG_LOGFILE_DIR}/traffic.out | \
        sed -e 's/^.* ts_lua //' -e 's/ gc_kb:.*gc_kb_max:.*threads:.*threads_max:.*$//' > lifecycle.out
    if diff lifecycle.out ${AUTEST_TEST_DIR}/gold/lifecycle.gold > /dev/null
    then
        exit 0
    fi
    let N=N-1
done
echo TIMEOUT
exit 1
