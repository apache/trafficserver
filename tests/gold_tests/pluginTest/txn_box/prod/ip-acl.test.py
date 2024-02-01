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
