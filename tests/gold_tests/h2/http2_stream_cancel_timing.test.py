'''
Test that canceled HTTP/2 streams are cleaned up in time.

This test reproduces issue #9179 where canceled streams via RST_STREAM
are not cleaned up in time, causing subsequent HEADERS frames to be
incorrectly refused with REFUSED_STREAM error.
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

Test.Summary = '''
Test that canceled HTTP/2 streams are cleaned up in time.
'''

Test.SkipUnless(Condition.HasProxyVerifierVersion('2.8.0'))

#
# Test stream cancellation timing
#
ts = Test.MakeATSProcess("ts", enable_tls=True)
replay_file = "replay/http2_stream_cancel_timing.replay.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)
ts.addDefaultSSLFiles()
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http2',
        'proxy.config.exec_thread.autoconfig.enabled': 0,
        'proxy.config.exec_thread.limit': 1,
        # Set max_concurrent_streams to 5 to reproduce the issue
        'proxy.config.http2.max_concurrent_streams_in': 5,
    })
ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

tr = Test.AddTestRun('Test that canceled streams are cleaned up in time')
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.AddVerifierClientProcess("client", replay_file, http_ports=[ts.Variables.port], https_ports=[ts.Variables.ssl_port])

# The test should pass - all 5 streams in the second batch should be accepted
tr.StillRunningAfter = ts
tr.StillRunningAfter = server

# Check that streams 11, 13, 15, 17, 19 are NOT refused
ts.Disk.traffic_out.Content += Testers.ExcludesExpression('stream=11.*REFUSED_STREAM', 'Stream 11 should not be refused')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression('stream=13.*REFUSED_STREAM', 'Stream 13 should not be refused')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression('stream=15.*REFUSED_STREAM', 'Stream 15 should not be refused')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression('stream=17.*REFUSED_STREAM', 'Stream 17 should not be refused')
ts.Disk.traffic_out.Content += Testers.ExcludesExpression('stream=19.*REFUSED_STREAM', 'Stream 19 should not be refused')
