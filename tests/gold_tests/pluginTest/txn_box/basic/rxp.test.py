# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
Test.Summary = '''
Regular expression.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun(
    "Regular Expressions",
    "rxp.replay.yaml",
    config_path='Auto',
    config_key="meta.txn_box.global",
    remap=[
        ['http://app.ex', 'http://app.ex', ['--key=meta.txn_box.remap', 'rxp.replay.yaml']],
        ['http://one.ex/path/', 'http://two.ex/path/'], ['http://one.ex']
    ])
ts = tr.Variables.TS
ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'txn_box'})
