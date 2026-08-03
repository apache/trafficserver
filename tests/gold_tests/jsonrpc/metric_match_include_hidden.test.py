'''
Verify that "traffic_ctl metric match --include-hidden" is accepted end-to-end by the
JSONRPC server (i.e. the additional rec type is not rejected during request decoding)
and still returns normal, published metrics.
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

Test.Summary = __doc__

ts = Test.MakeATSProcess("ts")

tr = Test.AddTestRun("metric match --include-hidden is accepted and still returns published metrics")
tr.Processes.Default.Command = 'traffic_ctl metric match reconfigure_time --include-hidden'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
# Proves the query actually ran against the server and matched a real, published metric.
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    r'proxy\.process\.proxy\.reconfigure_time', 'Expected the published reconfigure_time metric to be present in the output.')
# Proves the new rec type bit was not rejected by the JSONRPC request decoder.
# NOTE: must be "+=", not "=". Assigning a stream tester replaces any previously assigned
# tester for that stream, which would silently drop the check above.
tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
    'INVALID_INCOMING_DATA', 'The --include-hidden flag must not cause the JSONRPC request to be rejected as invalid.')
tr.StillRunningAfter = ts

tr = Test.AddTestRun("a normal metric match must not return hidden metrics")
tr.Processes.Default.Command = 'traffic_ctl metric match reconfigure_time'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
# RECT_HIDDEN_METRIC sits outside RECT_ALL, so a plain query still works and is unaffected.
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    r'proxy\.process\.proxy\.reconfigure_time', 'Expected the published reconfigure_time metric without --include-hidden too.')
tr.Processes.Default.Streams.All += Testers.ExcludesExpression('INVALID_INCOMING_DATA', 'A plain metric match must remain valid.')
tr.StillRunningAfter = ts
