# @file
#
# Copyright 2021, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Production use case: Manipulate specific query parameters.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "query.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Query",
    replay_file,
    remap=[
        ['http://one.ex', ('--key=meta.txn-box.remap.one', replay_file)],
        ['http://two.ex', ('--key=meta.txn-box.remap.two', replay_file)],
        ['http://three.ex', ('--key=meta.txn-box.remap.three', replay_file)], ['http://unmatched.ex/']
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
