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

# A bash script to wait for the log to be present and have a line for each request.  Also changes URL port
# number to PORT.

WAIT=120 # seconds
LINES=17
LOG_FILE="$1"
PORT=$2

while ((WAIT > 0))
do
    if [[ -f "$LOG_FILE" ]] ; then
        if (( $( wc -l < "$LOG_FILE" ) >= LINES )) ; then
            sed "s/:$PORT/:PORT/" "$LOG_FILE"
            exit $?
        fi
    fi
    sleep 1
    let WAIT=WAIT-1
done
exit 1
