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

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

Test.TxnBoxTestAndRun(
    "Smoke Test",
    "smoke.replay.yaml",
    config_path='Auto',
    config_key="meta.txn_box.global",
    remap=[
        ('http://example.one/3', 'http://example.one/3', ('--key=meta.txn_box.remap-1', 'smoke.replay.yaml')),
        ('http://example.one', 'http://example.one')
    ])
