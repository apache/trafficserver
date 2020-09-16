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

# Some TS code has very complex dependencies.  This make it impractical to unit test it with Catch, because it's
# too complicated to link a Catch executable.  Such code should be unit tested in the plugin for this Au test.

import os


Test.Summary = '''
Unit testing for code with complex dependencies.
'''

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    # NOTE: The PluginVC "overflow" test seems to get into an infinite loop when debug out is enabled.
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'pvc|pvc_event|Au_UT',
})

# File to be deleted when tests are fully completed.
#
InProgressFilePathspec = os.path.join(Test.RunDirectory, "in_progress")

Test.PrepareTestPlugin(
    os.path.join(os.path.join(Test.TestDirectory, ".libs"), "unit_testing.so"), ts,
    plugin_args=InProgressFilePathspec
)

# Create file to be deleted when tests are fully completed.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = "touch " + InProgressFilePathspec
tr.Processes.Default.ReturnCode = 0

# Give tests up to 60 seconds to complete.
#
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = (
    "N=60 ; while ((N > 0 )) ; do " +
    "if [[ ! -f " + InProgressFilePathspec + " ]] ; then exit 0 ; fi ; sleep 1 ; let N=N-1 ; " +
    "done ; echo 'TIMEOUT' ; exit 1"
)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
