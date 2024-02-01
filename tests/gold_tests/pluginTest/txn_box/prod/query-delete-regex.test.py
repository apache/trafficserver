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

replay_file = "query-delete.replay.yaml"

tr = Test.TxnBoxTestAndRun(
    "Query Delete with RXP",
    replay_file,
    config_path='Auto',
    config_key="meta.txn-box.alpha",
    verifier_client_args="--verbose info")

ts = tr.Variables.TS

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box',
        'proxy.config.http.cache.http': 0
    })
