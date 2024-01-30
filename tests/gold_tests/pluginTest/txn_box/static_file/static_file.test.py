'''
Static file serving and handling.
'''
# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

Test.Summary = '''
Server static file as response body.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

r = Test.TxnBoxTestAndRun(
    "Static file support",
    "static_file.replay.yaml",
    config_path='Auto',
    config_key="meta.txn-box.global",
    remap=[['http://base.ex', ['--key=meta.txn-box.remap', 'static_file.replay.yaml']]])
ts = r.Variables.TS
ts.Setup.Copy("static_file.txt", ts.Variables.CONFIGDIR)
ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'txn_box|http'})
