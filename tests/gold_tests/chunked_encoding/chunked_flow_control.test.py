'''
Regression tests for the chunked-tunnel flow-control throttle.
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
import sys

from ports import get_port

Test.Summary = __doc__

Test.ATSReplayTest(replay_file="replays/chunked_h1_flow_control.replay.yaml")
Test.ATSReplayTest(replay_file="replays/chunked_h2_flow_control.replay.yaml")
Test.ATSReplayTest(replay_file="replays/chunked_h2_origin_flow_control.replay.yaml")
Test.ATSReplayTest(replay_file="replays/chunked_passthru_flow_control.replay.yaml")

# A custom HTTP/2 origin sends the response header, waits for ATS to start the
# tunnel, then writes a body that pushes the chunked output buffer past high
# water together with the end of the stream. END_STREAM therefore arrives while
# the tunnel has the producer throttled. Each case gets its own ATS so its log
# assertions cannot be satisfied by another case.
ssl_dir = os.path.join(Test.Variables.AtsTestToolsDir, 'ssl')

h2_origin = Test.Processes.Process('h2-end-stream-origin')
h2_origin_port = get_port(h2_origin, 'https_port')
h2_origin.Setup.Copy(os.path.join(ssl_dir, 'server.pem'))
h2_origin.Setup.Copy(os.path.join(ssl_dir, 'server.key'))
h2_origin.Command = f'{sys.executable} {Test.TestDirectory}/h2_end_stream_origin.py {h2_origin_port} server.pem server.key'
h2_origin.Ready = When.PortOpen(h2_origin_port)
h2_origin.Streams.stdout = Testers.ExcludesExpression('queue_body_failed', 'The origin must queue every response body.')


def make_ts(name: str, deferred_event: str | None):
    """Configure an ATS for one case, expecting the deferral of deferred_event if given."""
    ts = Test.MakeATSProcess(name, enable_cache=False)
    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http_tunnel|http2_stream',
            'proxy.config.http.default_buffer_water_mark': 4096,
            'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            # One origin connection per request, so each starts with full flow-control windows.
            'proxy.config.http.server_session_sharing.match': 'none',
            # A dropped END_STREAM stalls the tunnel until this timeout, which then
            # finishes the response. The client gives up well before it fires.
            'proxy.config.http.transaction_no_activity_timeout_out': 10,
        })
    ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
        r'producer_handler \[http server VC_EVENT_INACTIVITY_TIMEOUT', 'No response may stall until the origin timeout.')
    if deferred_event is not None:
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            f'defer {deferred_event} until the read VIO is re-enabled', 'END_STREAM must arrive while the producer is throttled.')
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            f'rescheduled deferred {deferred_event}', 'The deferred END_STREAM must be rescheduled on re-enable.')
    return ts


# (path, frame that ends the response, event the stream defers)
response_cases = (
    ('data-end-stream', 'the last DATA frame', 'VC_EVENT_READ_COMPLETE'),
    ('trailers', 'trailing HEADERS', 'VC_EVENT_EOS'),
    # The GET request has already ended, so this empty frame closes the stream,
    # and the EOS that initiating_close() sends is not held back by a disabled
    # read VIO. Nothing is deferred; this guards that the path still completes.
    ('empty-end-stream', 'an empty DATA frame', None),
)
for index, (path, frame, deferred_event) in enumerate(response_cases):
    tr = Test.AddTestRun(f'HTTP/2 origin END_STREAM on {frame} -> HTTP/1.1 chunked client while throttled')
    ts = make_ts(f'ts-h2o-{path}', deferred_event)
    ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{h2_origin_port}/')
    if index == 0:
        tr.Processes.Default.StartBefore(h2_origin)
    tr.Processes.Default.StartBefore(ts)
    tr.MakeCurlCommand(
        f'-s --http1.1 --max-time 5 -o /dev/null -w "%{{size_download}} %{{http_code}}" http://127.0.0.1:{ts.Variables.port}/{path}',
        ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
        '^60000 200$', 'The client must receive the whole body and the terminating chunk.')
    tr.StillRunningAfter = h2_origin
