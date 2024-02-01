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
#  Copyright 2020, Verizon Media

import os.path

Test.Summary = '''
Test that redirect can be used to clip the path for specific URL paths.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = "redirect.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Redirect",
    replay_file,
    remap=[
        ['http://base.ex/', ('--key=meta.txn_box.remap', replay_file)],
        ['http://unmatched.ex/']  # no TxnBox for this rule.
        ,
        ['http://encode.ex/', ('--key=meta.txn_box.remap-encode', replay_file)],
        ['http://decode.ex/', ('--key=meta.txn_box.remap-decode', replay_file)]
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
