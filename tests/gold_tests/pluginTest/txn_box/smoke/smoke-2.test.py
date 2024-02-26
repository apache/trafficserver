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
'''
Basic smoke tests.
'''
Test.Summary = '''
Test basic functions and directives via remaps.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

replay_file = 'smoke-2.replay.yaml'
Test.TxnBoxTestAndRun(
    "Smoke 2 Test",
    replay_file,
    config_path='Auto',
    config_key="meta.txn_box.global",
    remap=[
        ['http://alpha.ex/', ('--key=meta.txn_box.remap.alpha', replay_file)],
        ['http://bravo.ex/', ('--key=meta.txn_box.remap.bravo', replay_file)]
    ])
