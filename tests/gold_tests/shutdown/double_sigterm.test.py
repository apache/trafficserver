'''
Verify that a second exit signal during the shutdown_timeout drain does not schedule shutdown again.
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

import os
import re
import sys

Test.Summary = '''
Verify that a second SIGTERM received while traffic_server drains for
proxy.config.stop.shutdown_timeout is ignored, so the shutdown path runs once.
'''

TS_PID_SCRIPT = 'ts_process_handler.py'
SHUTDOWN_TIMEOUT = 3

Test.Setup.Copy(os.path.join(Test.TestDirectory, '..', 'logging', TS_PID_SCRIPT))

ts = Test.MakeATSProcess('double_sigterm_ts')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'server',
        'proxy.config.stop.shutdown_timeout': SHUTDOWN_TIMEOUT,
    })

tr = Test.AddTestRun('Send SIGTERM twice during the shutdown_timeout drain')
tr.Processes.Default.StartBefore(ts)
# The signals must land on different SignalContinuation ticks (500ms apart), or they collapse into one flag.
tr.Processes.Default.Command = (
    f'{sys.executable} ./{TS_PID_SCRIPT} double_sigterm_ts --signal TERM && sleep 1 && '
    f'{sys.executable} ./{TS_PID_SCRIPT} double_sigterm_ts --signal TERM && sleep {SHUTDOWN_TIMEOUT + 2}')
tr.Processes.Default.ReturnCode = 0

ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    f'received exit signal, shutting down in {SHUTDOWN_TIMEOUT}secs', 'the first SIGTERM should schedule shutdown')
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    'received exit signal, shutdown already scheduled', 'the second SIGTERM should be ignored')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    'shutting down in.*shutting down in', 'shutdown should be scheduled only once', reflags=re.M | re.S)
