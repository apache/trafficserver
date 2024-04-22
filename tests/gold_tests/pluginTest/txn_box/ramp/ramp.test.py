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
import tempfile

Test.Summary = '''
Test traffic ramping.
'''

Test.SkipUnless(Condition.PluginExists("txn_box.so"))

RepeatCount = 100
replay_file = 'ramp.replay.yaml'

# Make a server representing a staging host to which we can ramp.
server_ramp = Test.MakeVerifierServerProcess('pv-server-staging', replay_file)
# Make it so that server-ramp is used in the txn_box config.
orig_replay_file = os.path.join(Test.TestDirectory, replay_file)
old_replay_content = open(orig_replay_file).read()
new_replay_content = old_replay_content.replace('{server_port}', str(server_ramp.Variables.http_port))
# There's no need to delete temp_replay because it will be cleaned up with the TestDirectory.
temp_replay = tempfile.NamedTemporaryFile(dir=Test.RunDirectory, delete=False)
# Reference this new replay file for all other TestRun processes.
replay_file = temp_replay.name
open(replay_file, 'w').write(new_replay_content)

tr = Test.TxnBoxTestAndRun(
    "Ramping",
    replay_file,
    remap=[('http://one.ex', 'http://three.ex', ('--key=meta.txn_box.remap', replay_file))],
    verifier_client_args=f"--verbose diag --repeat {RepeatCount}",
    verifier_server_args=f"--verbose diag")

ts = tr.Variables.TS
ts.StartBefore(server_ramp)
ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR)
ts.Setup.Copy("ramp.logging.yaml", os.path.join(ts.Variables.CONFIGDIR, "logging.yaml"))
ts.Disk.records_config.update({'proxy.config.log.max_secs_per_buffer': 1})
