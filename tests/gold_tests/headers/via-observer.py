'''
Extract the protocol information from the VIA headers and store it in a log file for later verification.
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

log = open('via.log', 'w')
rxp = re.compile('(^.*) [\w._-]+\[.*$')

def observe(headers):
    if 'via' in headers:
        via = headers['via']
        if via:
            via = rxp.sub(r'\1', via)
        else:
            via = ''
    else:
        via = '---missing---'
    log.write("Via: {}\n".format(via))
    log.flush()

Hooks.register(Hooks.ReadRequestHook, observe)
