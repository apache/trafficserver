# @file
#
# Copyright 2021, Yahoo!
# SPDX-License-Identifier: Apache-2.0
#
Test.Summary = '''
txn-error directive
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "txn-error.replay.yaml"
records_config_tweaks = {'proxy.config.diags.debug.enabled': 1, 'proxy.config.diags.debug.tags': 'txn_box'}

suffix = "-err"
err_run = Test.TxnBoxTestAndRun(
    "txn-error",
    replay_file,
    config_path='Auto',
    config_key="meta.txn_box.global",
    verifier_client_args="--keys base-case no-fixup",
    suffix=suffix)
err_test = err_run.Variables.TS

err_test.Disk.records_config.update(records_config_tweaks)
err_test.Disk.diags_log.Content += Testers.ContainsExpression("ua-req", "Verify hook logging is happening.")
err_test.Disk.diags_log.Content += Testers.ExcludesExpression("pre-remap", "Verify no pre-remap callbacks.")

suffix = "-bypass"
bp_run = Test.TxnBoxTestAndRun(
    "txn-error bypass",
    replay_file,
    config_path='Auto',
    config_key="meta.txn_box.global",
    verifier_client_args="--keys bypass-case",
    suffix=suffix)
bp_test = bp_run.Variables.TS
bp_test.Disk.records_config.update(records_config_tweaks)

bp_test.Disk.diags_log.Content += Testers.ContainsExpression("ua-req", "Verify hook logging is happening.")
bp_test.Disk.diags_log.Content += Testers.ContainsExpression("pre-remap", "Verify no pre-remap callbacks.")
