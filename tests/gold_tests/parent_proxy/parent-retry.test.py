"""
Test parent_retry config settings
"""
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

Test.testName = "Test parent_retry settings"
Test.ContinueOnFail = True


class ParentRetryTest:
    """
    Test loading parent.config with parent_retry setting enabled
    """
    ts_parent_hostname = "localhost:8081"

    def __init__(self):
        """Initialize the test."""
        self._configure_ts_child()

    def _configure_ts_child(self):
        self.ts_child = Test.MakeATSProcess("ts_child")
        self.ts_child.Disk.parent_config.AddLine(
            f'dest_domain=. method=get parent="{self.ts_parent_hostname}" parent_retry=unavailable_server_retry unavailable_server_retry_responses="502,503"'
        )

    def run(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.ts_child)
        tr.Processes.Default.Command = f'curl "{self.ts_child.Variables.port}" --verbose'
        tr.StillRunningAfter = self.ts_child


ParentRetryTest().run()
