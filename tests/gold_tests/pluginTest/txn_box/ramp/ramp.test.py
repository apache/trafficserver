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
Test traffic ramping.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

RepeatCount = 1000

tr = Test.TxnBoxTestAndRun(
    "Ramping",
    "ramp.replay.yaml",
    remap=[('http://one.ex', 'http://three.ex', ('--key=meta.txn_box.remap', 'ramp.replay.yaml'))],
    verifier_client_args="--verbose diag --repeat {}".format(RepeatCount))

with open(f"{tr.TestDirectory}/multi_ramp_common.py") as f:
    code = compile(f.read(), "multi_ramp_common.py", 'exec')
    exec(code)

ts = tr.Variables.TS
ts.Setup.Copy("ramp.replay.yaml", ts.Variables.CONFIGDIR)
ts.Setup.Copy("ramp.logging.yaml", os.path.join(ts.Variables.CONFIGDIR, "logging.yaml"))
ts.Disk.records_config.update({'proxy.config.log.max_secs_per_buffer': 1})
