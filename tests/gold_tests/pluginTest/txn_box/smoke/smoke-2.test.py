# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
'''
Basic smoke tests.
'''
Test.Summary = '''
Test basic functions and directives via remaps.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = 'smoke-2.replay.yaml'
Test.TxnBoxTestAndRun(
    "Smoke 2 Test",
    replay_file,
    config_path='Auto',
    config_key="meta.txn_box.global",
    remap=[
        ['http://alpha.ex/', ('--key=meta.txn_box.remap.alpha', replay_file)],
        ['http://bravo.ex/', ('--key=meta.txn_box.remap.bravo', replay_file)]
    ])
