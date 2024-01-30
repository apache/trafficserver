# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
Test.Summary = '''
Multiple Remap Configurations.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

# Point of this is to test two remap configs in isolation and then both as separate remap configs.
tr = Test.TxnBoxTestAndRun(
    "Multiple remap configurations",
    "multi-cfg.replay.yaml",
    remap=[
        ["http://one.ex", ['multi-cfg.1.yaml']], ["http://two.ex", ['multi-cfg.2.yaml']],
        ["http://both.ex", ['multi-cfg.1.yaml', 'multi-cfg.2.yaml']]
    ])
ts = tr.Variables.TS
ts.Setup.Copy("multi-cfg.1.yaml", ts.Variables.CONFIGDIR)
ts.Setup.Copy("multi-cfg.2.yaml", ts.Variables.CONFIGDIR)
ts.Disk.records_config.update(
    {
        'proxy.config.log.max_secs_per_buffer': 1,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box'
    })
