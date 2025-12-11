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

import json
import os

Test.Summary = '''
Test traffic_top batch mode with JSON and text output.
'''

Test.ContinueOnFail = True


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

# Test 1: JSON output format
tr = helper.add_test("traffic_top JSON output")
tr.Processes.Default.Command = "traffic_top -b -j -c 1"
tr.Processes.Default.ReturnCode = 0
# Verify JSON is valid by parsing it
tr.Processes.Default.Streams.stdout = Testers.Lambda(
    lambda output: json.loads(output.strip()) is not None, "Output should be valid JSON")

# Test 2: JSON output contains expected fields
tr2 = helper.add_test("traffic_top JSON contains required fields")
tr2.Processes.Default.Command = "traffic_top -b -j -c 1"
tr2.Processes.Default.ReturnCode = 0


def check_json_fields(output):
    """Check that JSON output contains expected fields."""
    try:
        data = json.loads(output.strip())
        required_fields = ['timestamp', 'host']
        for field in required_fields:
            if field not in data:
                return False, f"Missing required field: {field}"
        return True, "All required fields present"
    except json.JSONDecodeError as e:
        return False, f"Invalid JSON: {e}"


tr2.Processes.Default.Streams.stdout = Testers.Lambda(
    lambda output: check_json_fields(output)[0], "JSON should contain required fields")

# Test 3: Text output format
tr3 = helper.add_test("traffic_top text output")
tr3.Processes.Default.Command = "traffic_top -b -c 1"
tr3.Processes.Default.ReturnCode = 0
# Text output should have header and data lines
tr3.Processes.Default.Streams.stdout = Testers.ContainsExpression("TIMESTAMP", "Text output should contain TIMESTAMP header")

# Test 4: Multiple iterations
tr4 = helper.add_test("traffic_top multiple iterations")
tr4.Processes.Default.Command = "traffic_top -b -j -c 2 -s 1"
tr4.Processes.Default.ReturnCode = 0


def check_multiple_lines(output):
    """Check that we got multiple JSON lines."""
    lines = output.strip().split('\n')
    if len(lines) < 2:
        return False, f"Expected 2 lines, got {len(lines)}"
    # Each line should be valid JSON
    for line in lines:
        try:
            json.loads(line)
        except json.JSONDecodeError as e:
            return False, f"Invalid JSON line: {e}"
    return True, "Got multiple valid JSON lines"


tr4.Processes.Default.Streams.stdout = Testers.Lambda(
    lambda output: check_multiple_lines(output)[0], "Should have multiple JSON lines")

# Test 5: Help output
tr5 = helper.add_test("traffic_top help")
tr5.Processes.Default.Command = "traffic_top --help"
tr5.Processes.Default.ReturnCode = 0
tr5.Processes.Default.Streams.stdout = Testers.ContainsExpression("batch", "Help should mention batch mode")

# Test 6: Version output
tr6 = helper.add_test("traffic_top version")
tr6.Processes.Default.Command = "traffic_top --version"
tr6.Processes.Default.ReturnCode = 0
tr6.Processes.Default.Streams.stdout = Testers.ContainsExpression("traffic_top", "Version should contain program name")
