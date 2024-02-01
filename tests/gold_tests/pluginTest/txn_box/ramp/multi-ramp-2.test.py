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
Multi-bucketing (style 2).
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

RepeatCount = 1000
CFG_PATH = "multi-ramp-2.cfg.yaml"
tr = Test.TxnBoxTestAndRun(
    "Multi bucketing 2",
    "multi-ramp.replay.yaml",
    remap=[['http://base.ex/', 'http://base.ex/', [CFG_PATH]], ['https://base.ex/', 'https://base.ex/', [CFG_PATH]]],
    verifier_client_args='--verbose info --format "{{url}}" --repeat {}'.format(RepeatCount),
    verifier_server_args='--verbose info --format "{url}"',
    enable_tls=True)

with open("{tr.TestDirectory}/multi_ramp_common.pymulti_ramp_common.py") as f:
    code = compile(f.read(), "multi_ramp_common.py", 'exec')
    exec(code)

ramp_test_fixup(tr)
ts = tr.Variables.TXNBOX_TS
ts.Setup.Copy(CFG_PATH, ts.Variables.CONFIGDIR)
