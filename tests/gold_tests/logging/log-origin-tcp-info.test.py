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

import os
import shlex
import sys

from ports import get_port

Test.Summary = 'Verify origin TCP_INFO fields and sampling controls'
Test.ContinueOnFail = True
Test.SkipUnless(Condition.IsPlatform('linux'), Condition.PluginExists('header_rewrite.so'))

for mode in ('disabled', 'enabled', 'retry'):
    tr = Test.ATSReplayTest(replay_file=f'replay/origin-tcp-info-{mode}.replay.yaml')
    ts = getattr(tr.Processes, f'ts_{mode}')
    if mode == 'retry':
        server = tr.Processes.server_retry
        get_port(ts, 'closed_port')
        ts.Disk.parent_config.AddLine(
            f'dest_domain=. parent="127.0.0.1:{server.Variables.http_port};127.0.0.1:{ts.Variables.closed_port}" '
            'round_robin=false go_direct=false parent_is_proxy=true parent_retry=simple_retry '
            'simple_server_retry_responses="503" max_simple_retries=1')
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            f'open connection to .*127\\.0\\.0\\.1:{ts.Variables.closed_port}', 'The retry must attempt the second parent')
    log_path = os.path.join(ts.Variables.LOGDIR, 'origin_tcp_info.log')
    checker = os.path.join(Test.TestDirectory, 'verify_origin_tcp_info.py')
    tr.Processes.Default.Command += (f' && {shlex.quote(sys.executable)} {shlex.quote(checker)} {shlex.quote(log_path)} {mode}')
    tr.Processes.Default.TimeOut = 30
    tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
        f'PASS: origin TCP_INFO sampling {mode}', f'Validate all four origin TCP_INFO fields with sampling {mode}')

Test.ATSReplayTest(replay_file='replay/origin-tcp-info-global-disabled.replay.yaml')
