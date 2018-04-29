'''
Extract the protocol information from the FORWARDED headers and store it in a log file for later verification.
'''
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

import re
import subprocess

log = open('forwarded.log', 'w')

regexByEqualUuid = re.compile(r'^by=_[0-9a-f-]+$')

byCount = 0
byEqualUuid = "__INVALID__"

def observe(headers):

    global byCount
    global byEqualUuid

    seen = False
    for h in headers.items():
        if h[0].lower() == "forwarded":

            content = h[1]

            if content.startswith("by="):

                byCount += 1

                if ((byCount == 4) or (byCount == 5)) and regexByEqualUuid.match(content):  # "by" should give UUID

                    # I don't think there is a way to know what UUID traffic_server generates, so I just do a crude format
                    # check and make sure the same value is used consistently.

                    byEqualUuid = content

            content = content.replace(byEqualUuid, "__BY_EQUAL_UUID__", 1)

            log.write(content + "\n")
            seen = True

    if not seen:
        log.write("FORWARDED MISSING\n")
    log.write("-\n")
    log.flush()


Hooks.register(Hooks.ReadRequestHook, observe)
