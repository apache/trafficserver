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

# Wait till remap.config finishes loading two times.

WAIT=60
LOG_FILE="$1"

while (( WAIT > 0 ))
do
    N=$( grep -F 'NOTE: remap.config finished loading' $LOG_FILE | wc -l )
    if [[ $N = 2 ]] ; then
        exit 0
    fi
    sleep 1
    let WAIT=WAIT-1
done
echo TIMEOUT
exit 1
