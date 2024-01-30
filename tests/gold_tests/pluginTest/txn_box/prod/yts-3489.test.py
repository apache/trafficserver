# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Production use case: Use redirect to clip the path for specific URL paths.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "yts-3489.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Redirect",
    replay_file,
    remap=[
        ['http://base.ex/', ('--key=meta.txn-box.remap', replay_file)],
        ['http://alt.ex/', ('--key=meta.txn-box.remap-alt', replay_file)],
        ['http://join.ex/', ('--key=meta.txn-box.remap-join', replay_file)],
        ['http://concat.ex/', ('--key=meta.txn-box.remap-concat', replay_file)], ['http://unmatched.ex/']
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
