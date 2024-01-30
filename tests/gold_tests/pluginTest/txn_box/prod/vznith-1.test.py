# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Production use case: Use query parameters to modify the proxy request path in remap.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "vznith-1.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Redirect",
    replay_file,
    remap=[['http://base.ex/edge/file', ('--key=meta.txn-box.remap', replay_file)], ['http://unmatched.ex/']],
    verifier_client_args="--verbose info")

ts = tr.Variables.TS

ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR)  # because it's remap only - not auto-copied.

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0
    })
