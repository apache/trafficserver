# @file
#
# Copyright 2021, Yahoo!
# SPDX-License-Identifier: Apache-2.0
#
Test.Summary = '''
txn-debug directive
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

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
ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'txn_box'})

ts.Disk.traffic_out.Content += Testers.ContainsExpression(r"DIAG: <HttpSM.cc", "Verify that there was transaction level debugging.")

tr = Test.TxnBoxTestAndRun(
    "Test txn-debug disabled",
    replay_file,
    config_path='Auto',
    verifier_client_args="--verbose diag --keys debug-not-expected",
    config_key="meta.txn_box.global",
    suffix="debug-disabled")
tr.DelayStart = 20
ts = tr.Variables.TS
ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'txn_box'})

ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    r"DIAG: <HttpSM.cc", "Verify that there was not transaction level debugging.")
