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
#  Copyright 2021, Verizon Media
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
