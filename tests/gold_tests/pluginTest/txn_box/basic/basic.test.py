# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
'''
Basic smoke tests.
'''
Test.Summary = '''
Test basic functions and directives.
'''

Test.SkipUnless(Condition.PluginExists('txn_box.so'))

tr = Test.TxnBoxTestAndRun(
    "Test basics",
    "basic.replay.yaml",
    config_path='Auto',
    config_key="meta.txn_box.global",
    remap=[
        ['http://remap.ex', 'http://remapped.ex', ['--key=meta.txn_box.remap-1', 'basic.replay.yaml']],
        ['http://one.ex/path/', 'http://two.ex/path/'], ['http://one.ex']
    ])
ts = tr.Variables.TS
ts.Disk.records_config.update(
    {
        'proxy.config.log.max_secs_per_buffer': 1,
        'proxy.config.http.per_server.connection.max': 500,
        'proxy.config.http.background_fill_completed_threshold': 0.4,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box'
    })
