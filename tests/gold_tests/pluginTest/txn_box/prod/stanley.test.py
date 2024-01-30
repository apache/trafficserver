# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Production use case: Stanley's "remap" plugin.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "stanley.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Redirect",
    replay_file,
    remap=[
        ['http://nowhere.com', 'http://0.0.0.0/health', ('--key=meta.txn-box.remap', replay_file)],
        ['http://calendar.yahoo.com/', 'http://0.0.0.0/health', ('--key=meta.txn-box.remap', replay_file)],
        ['/', 'http://0.0.0.0/health', ('--key=meta.txn-box.remap', replay_file)]
    ],
    verifier_client_args="--verbose info")

ts = tr.Variables.TS

ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR)  # because it's remap only - not auto-copied.

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0
    })
