# @file
#
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
#
#  Copyright 2021, Yahoo Inc
#
Test.Summary = '''
txn-debug directive
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))
Test.SkipIf(Condition.true("This needs to be revisit. TS not finishing up gracefully."))

replay_file = "txn-debug.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Test txn-debug enabled",
    replay_file,
    config_path='Auto',
    verifier_client_args="--verbose diag --keys debug-expected",
    verifier_server_args="--verbose diag",
    config_key="meta.txn_box.global",
    suffix="debug-enabled")
ts = tr.Variables.TS
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.crash_log_helper':
            '/home/dmeden/code/git/trafficserver/build/_sandbox/txn-debug/ts-debug-enabled/bin/traffic_crashlog'
    })

ts.Disk.traffic_out.Content += Testers.ContainsExpression(r"DIAG: <HttpSM.cc", "Verify that there was transaction level debugging.")

tr = Test.TxnBoxTestAndRun(
    "Test txn-debug disabled",
    replay_file,
    config_path='Auto',
    verifier_client_args="--verbose diag --keys debug-not-expected",
    config_key="meta.txn_box.global",
    suffix="debug-disabled")
ts = tr.Variables.TS
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.crash_log_helper':
            '/home/dmeden/code/git/trafficserver/build/_sandbox/txn-debug/ts-debug-disabled/bin/traffic_crashlog'
    })

ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r"DIAG: <HttpSM.cc", "Verify that there was not transaction level debugging.")
