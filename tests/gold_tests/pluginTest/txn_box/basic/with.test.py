# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
'''
Basic smoke tests.
'''
Test.Summary = '''
Some test for the with directive.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun("With testing", "with.replay.yaml", config_path='Auto', config_key="meta.txn_box.global")
ts = tr.Variables.TS
ts.Disk.records_config.update(
    {
        'proxy.config.log.max_secs_per_buffer': 1,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box'
    })
