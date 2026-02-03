'''
Test that header_rewrite's run-plugin operator works with relative plugin paths
when header_rewrite is used as a remap plugin without a geo database.

This test verifies the fix for YTSATS-4852.
'''
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

Test.Summary = '''
Test run-plugin operator with relative plugin path in remap mode
'''

Test.ContinueOnFail = True

# Define ATS process. Note that we do NOT use header_rewrite as a global
# plugin - only as a remap plugin. This is important because the bug only
# manifests when header_rewrite is used only as a remap plugin without a
# geo database.
ts = Test.MakeATSProcess("ts")

Test.testName = ""

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.show_location': 0,
        'proxy.config.diags.debug.tags': 'header_rewrite|plugin_factory',
    })

# Copy the run_plugin.conf rule file to the test directory.
ts.Setup.CopyAs('rules/run_plugin.conf', Test.RunDirectory)

# Use header_rewrite as a remap plugin with run-plugin calling generator.so
# using a relative path. The generator plugin will intercept and generate a
# response based on the URL path.
ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:65535'
    ' @plugin=header_rewrite.so @pparam={0}/run_plugin.conf'.format(Test.RunDirectory))

# Test that run-plugin with a relative path works.
# The generator plugin expects URLs like /cache/SIZE or /nocache/SIZE.
# It will generate a response with SIZE bytes.
tr = Test.AddTestRun("run-plugin with relative path")
tr.Processes.Default.Command = (
    'curl --proxy 127.0.0.1:{0} "http://www.example.com/cache/100" -H "Host: www.example.com" --verbose 2>&1'.format(
        ts.Variables.port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(Test.Processes.ts)
# The generator plugin should return a 200 OK with 100 bytes of content.
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("200 OK", "Expected 200 OK response from generator plugin")
tr.StillRunningAfter = ts

# Verify that the plugin was loaded successfully by checking the debug output.
# If the fix is not applied, we would see "failed to find plugin 'generator.so'"
# in the traffic.out log.
ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
    "failed to find plugin", "Should not fail to find the plugin with relative path")
ts.Disk.traffic_out.Content += Testers.ExcludesExpression("Unable to load plugin", "Should not fail to load the plugin")
