# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Test the proxy-reply directive.
'''
Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "reply.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "proxy-reply",
    replay_file,
    config_path='Auto',
    config_key='meta.txn-box.global',
    remap=[['http://base.ex/', ('--key=meta.txn-box.remap', replay_file)], ['http://unmatched.ex/']],
    verifier_client_args="--verbose info")

ts = tr.Variables.TS

ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR)  # because it's remap only - not auto-copied.

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box|http',
        'proxy.config.http.cache.http': 0
    })
