# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
'''
Basic smoke tests.
'''
Test.Summary = '''
Example: Force accept encoding.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

tr = Test.TxnBoxTestAndRun("Accept Encoding", "accept-encoding.replay.yaml", config_path='Auto', config_key="meta.txn_box.global")

tr.Variables.TS.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0,
        'proxy.config.http.normalize_ae': 0,
        'proxy.config.url_remap.remap_required': 0
    })
