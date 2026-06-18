'''
Verify that a normal (SIGTERM) shutdown does not spuriously fire traffic_crashlog.
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

Test.Summary = '''
Verify that gracefully stopping traffic_server (SIGTERM) does not cause the crash
log helper (traffic_crashlog) to emit a bogus crash log.

Regression test: the crash log helper is parked with SIGSTOP and arms a
parent-death signal (PR_SET_PDEATHSIG=SIGCONT). When traffic_server exits
normally -- e.g. a routine SIGTERM shutdown -- the kernel resumes the helper via
that death signal. The helper used to decide "did I wake because of a real crash
or a plain exit?" with a racy getppid() check, and would lose the race and write
an empty crash log against the already-exiting process. The fix replaces that
guess with a handshake: crash_logger_invoke() writes the signal number to the
helper, and a plain exit closes the socket so the helper reads EOF and stays
quiet.

Note: PR_SET_PDEATHSIG is a no-op on Darwin, so this false positive only occurs
on Linux. On other platforms this test still confirms that the helper is armed
and does not emit a crash log on a clean shutdown.
'''

ts = Test.MakeATSProcess('ts')

ts.Disk.records_config.update(
    {
        # Enable the "server" debug tag so traffic_server emits the "received exit signal,
        # shutting down" DIAG line (the shutdown anchor checked below) to traffic.out.
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'server',
        # Force the crash log helper on regardless of the build's TS_USE_REMOTE_UNWINDING
        # default (it is NULL on platforms without remote unwinding, e.g. Darwin).
        'proxy.config.crash_log_helper': os.path.join(ts.Variables.BINDIR, 'traffic_crashlog'),
    })

tr = Test.AddTestRun('Start traffic_server with the crash log helper armed, then let AuTest SIGTERM it')
tr.Processes.Default.Command = 'printf "crash log helper armed"'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# Anchor the scenario: confirm traffic_server actually ran the graceful (SIGTERM) shutdown
# path -- the exact trigger that woke the crash log helper in production. Without this the
# ExcludesExpression checks below could pass without ever exercising shutdown. (The crash
# log helper itself is forced on via proxy.config.crash_log_helper above, and debug/DIAG
# output is emitted to traffic.out.)
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    'received exit signal, shutting down', 'traffic_server should have run the graceful shutdown path')

# The crash log helper shares traffic_server's stderr (bound to traffic.out). On a clean
# shutdown it must neither wake nor write a crash log.
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    'crashlog started', 'the crash log helper must not wake on a clean SIGTERM shutdown')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    'wrote crash log', 'a clean SIGTERM shutdown must not produce a crash log')
