'''
Static file serving and handling.
'''
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
