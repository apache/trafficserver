# @file
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
#  Copyright 2021, Yahoo Inc
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
