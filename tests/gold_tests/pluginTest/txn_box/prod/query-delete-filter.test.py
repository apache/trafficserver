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

replay_file = "query-delete.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Query Delete with RXP",
    replay_file,
    config_path='Auto',
    config_key="meta.txn-box.bravo",
    verifier_client_args="--verbose info")

ts = tr.Variables.TS

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0
    })
