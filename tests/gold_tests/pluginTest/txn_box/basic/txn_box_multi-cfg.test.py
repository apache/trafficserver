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

Test.Summary = '''
Multiple Remap Configurations.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

# Point of this is to test two remap configs in isolation and then both as separate remap configs.
tr = Test.TxnBoxTestAndRun(
    "Multiple remap configurations",
    "multi-cfg.replay.yaml",
    remap=[
        ["http://one.ex", ['multi-cfg.1.yaml']], ["http://two.ex", ['multi-cfg.2.yaml']],
        ["http://both.ex", ['multi-cfg.1.yaml', 'multi-cfg.2.yaml']]
    ])
ts = tr.Variables.TS
ts.Setup.Copy("multi-cfg.1.yaml", ts.Variables.CONFIGDIR)
ts.Setup.Copy("multi-cfg.2.yaml", ts.Variables.CONFIGDIR)
ts.Disk.records_config.update(
    {
        'proxy.config.log.max_secs_per_buffer': 1,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box'
    })
