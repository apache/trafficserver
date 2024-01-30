# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Test using IP spaces as ACLs.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "ip-acl.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "IP ACL",
    replay_file,
    config_path='Auto',
    config_key='meta.txn-box.global',
    remap=[['http://base.ex/'], ['http://docjj.ex/', 'http://docjj.ex', ['--key=meta.txn-box.remap', 'ip-acl.replay.yaml']]],
    verifier_client_args="--verbose info")

ts = tr.Variables.TS

ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR)  # because it's remap only - not auto-copied.
ts.Setup.Copy("ip-acl.csv", ts.Variables.CONFIGDIR)  # Need the IP Space file.

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0
    })
