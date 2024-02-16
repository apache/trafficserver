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
'''
Basic smoke tests.
'''
Test.Summary = '''
Test basic functions and directives.
'''

Test.SkipUnless(Condition.PluginExists('txn_box.so'))

tr = Test.TxnBoxTestAndRun(
    "Test basics",
    "basic.replay.yaml",
    config_path='Auto',
    config_key="meta.txn_box.global",
    remap=[
        ['http://remap.ex', 'http://remapped.ex', ['--key=meta.txn_box.remap-1', 'basic.replay.yaml']],
        ['http://one.ex/path/', 'http://two.ex/path/'], ['http://one.ex']
    ])
ts = tr.Variables.TS
ts.Disk.records_config.update(
    {
        'proxy.config.log.max_secs_per_buffer': 1,
        'proxy.config.http.per_server.connection.max': 500,
        'proxy.config.http.background_fill_completed_threshold': 0.4,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_box'
    })
