# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
Test.Summary = '''
Modifier checks.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = 'mod.replay.yaml'
tr = Test.TxnBoxTestAndRun(
    "Test modifiers",
    replay_file,
    remap=[
        ['http://alpha.ex', ['--key=meta.txn-box.alpha', replay_file]],
        ['http://bravo.ex', ['--key=meta.txn-box.bravo', replay_file]],
        ['http://charlie.ex', ['--key=meta.txn-box.charlie', replay_file]]
    ])
ts = tr.Variables.TS
ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR)  # because it's remap only - not auto-copied.
ts.Disk.records_config.update(
    {
        'proxy.config.http.cache.http': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box'
    })
