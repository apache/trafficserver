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
"""
Test traffic_top batch mode output.
"""

import os

Test.Summary = '''
Test traffic_top batch mode with JSON and text output.
'''

Test.ContinueOnFail = True

# Get traffic_top path - try ATS_BIN first (from --ats-bin), fallback to BINDIR
ats_bin = os.environ.get('ATS_BIN', Test.Variables.BINDIR)
traffic_top_path = os.path.join(ats_bin, 'traffic_top')

# If running from build directory, the path structure is different
if not os.path.exists(traffic_top_path):
    # Try the build directory structure
    build_path = os.path.join(os.path.dirname(ats_bin), 'src', 'traffic_top', 'traffic_top')
    if os.path.exists(build_path):
        traffic_top_path = build_path


class TrafficTopHelper:
    """Helper class for traffic_top tests."""

    def __init__(self, test):
        self.test = test
        self.ts = test.MakeATSProcess("ts")
        self.test_number = 0

    def add_test(self, name):
        """Add a new test run."""
        tr = self.test.AddTestRun(name)
        if self.test_number == 0:
            tr.Processes.Default.StartBefore(self.ts)
        self.test_number += 1
        tr.Processes.Default.Env = self.ts.Env
        tr.DelayStart = 2
        tr.StillRunningAfter = self.ts
        return tr


# Create the helper
helper = TrafficTopHelper(Test)

# Test 1: JSON output format - check for JSON structure markers
tr = helper.add_test("traffic_top JSON output")
tr.Processes.Default.Command = f"{traffic_top_path} -b -j -c 1"
tr.Processes.Default.ReturnCode = 0
# JSON output should contain timestamp and host fields
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('"timestamp"', "JSON should contain timestamp field")

# Test 2: JSON output contains host field
tr2 = helper.add_test("traffic_top JSON contains host field")
tr2.Processes.Default.Command = f"{traffic_top_path} -b -j -c 1"
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression('"host"', "JSON should contain host field")

# Test 3: Text output format
tr3 = helper.add_test("traffic_top text output")
tr3.Processes.Default.Command = f"{traffic_top_path} -b -c 1"
tr3.Processes.Default.ReturnCode = 0
# Text output should have header and data lines
tr3.Processes.Default.Streams.stdout = Testers.ContainsExpression("TIMESTAMP", "Text output should contain TIMESTAMP header")

# Test 4: Help output (argparse returns 64 for --help)
tr4 = helper.add_test("traffic_top help")
tr4.Processes.Default.Command = f"{traffic_top_path} --help"
tr4.Processes.Default.ReturnCode = 64
tr4.Processes.Default.Streams.stderr = Testers.ContainsExpression("batch", "Help should mention batch mode")

# Test 5: Version output
tr5 = helper.add_test("traffic_top version")
tr5.Processes.Default.Command = f"{traffic_top_path} --version"
tr5.Processes.Default.ReturnCode = 0
tr5.Processes.Default.Streams.stdout = Testers.ContainsExpression("traffic_top", "Version should contain program name")
