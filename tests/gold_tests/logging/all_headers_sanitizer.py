'''
Sanitize the ATS-generated custom log file from the all_headers test.
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

import sys
import re

rexl = []
rexl.append((re.compile(r"\{\{Date\}\:\{[^}]*\}\}"), "({__DATE__}}"))
rexl.append((re.compile(r"\{\{Expires\}\:\{[^}]*\}\}"), "({__EXPIRES__}}"))
rexl.append((re.compile(r"\{\{Last-Modified\}\:\{[^}]*\}\}"), "({__LAST_MODIFIED__}}"))
rexl.append((re.compile(r"\{\{Server\}\:\{ATS/[0-9.]*\}\}"), "({__ATS_SERVER__}}"))
rexl.append((re.compile(r"\{\{Server\}\:\{ECS [^}]*\}\}"), "({__ECS_SERVER__}}"))
rexl.append((re.compile(r"\{\{Via\}\:\{[^}]*\}\}"), "({__VIA__}}"))
rexl.append((re.compile(r"\{\{Server\}\:\{ApacheTrafficServer/[0-9.]*\}\}"), "({__ATS2_SERVER__}}"))
rexl.append((re.compile(r"\{\{Age\}\:\{[0-9]*\}\}"), "({__AGE__}}"))
rexl.append((re.compile(r"\:" + sys.argv[1]), "__TS_PORT__")) # 1st and only argument is TS client port

# Handle inconsistencies which I think are caused by different revisions of the standard Python http.server.HTTPServer class.

rexl.append((re.compile(r'\{"359670651[^"]*"\}'), '{"{359670651__WEIRD__}"}'))
rexl.append((re.compile(r'\{\{Accept-Ranges\}:\{bytes\}\}'), ''))

for line in sys.stdin:
    for rex, subStr in rexl:
        line = rex.sub(subStr, line)

    print(line)
