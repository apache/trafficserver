# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
Test.Summary = '''
Comparison checks.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun("Test comparisons", "cmp.replay.yaml", config_path='Auto', config_key="meta.txn_box.global")
ts = tr.Variables.TS
ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'txn_box'})
