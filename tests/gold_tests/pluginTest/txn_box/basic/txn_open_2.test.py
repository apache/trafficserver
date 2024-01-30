# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
'''
Txn Open (start) hook testing.
'''
Test.Summary = '''
Test transaction start hook.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun(
    "Test txn_open 2", "txn_open_2.replay.yaml", config_path='Auto', config_key="meta.txn_box.global", remap=[["http://one.ex"]])

ts = tr.Variables.TS
ts.Disk.records_config.update(
    {
        'proxy.config.log.max_secs_per_buffer': 1,
        'proxy.config.http.transaction_no_activity_timeout_out': 120,
        'proxy.config.http.per_server.connection.max': 500,
        'proxy.config.http.background_fill_completed_threshold': 0.4,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box'
    })
