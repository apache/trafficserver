'''
Test the verify_remap_plugin TrafficServer command.
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

import os

Test.Summary = '''
Test that the TrafficServer verify_remap_plugin command works as expected.
'''

process_counter = 0


def create_ts_process():
    """
    Create a unique ATS process with each call to this function.
    """
    global process_counter
    process_counter += 1

    ts = Test.MakeATSProcess("ts{counter}".format(counter=process_counter))

    # Ideally we would set the test run's Processes.Default to ts, but deep
    # copy of processes is not currently implemented in autest. Therefore we
    # replace the command which ts runs with a dummy command, and pull in
    # piecemeal the values from ts that we want into the test run.
    ts.Command = "sleep 100"
    # sleep will return -2 when autest kills it. We set the expectation for the
    # -2 return code here so the test doesn't fail because of this.
    ts.ReturnCode = -2
    # Clear the ready criteria because sleep is ready as soon as it is running.
    ts.Ready = None
    return ts


"""
TEST: verify_remap_plugin should complain if an argument is not passed to it.
"""
tr = Test.AddTestRun("Verify the requirement of an argument")
ts = create_ts_process()
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = "traffic_server -C 'verify_remap_plugin'"
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: verifying a plugin requires a plugin SO file path argument", "Should warn about the need for an SO file argument")
"""
TEST: verify_remap_plugin should complain if the argument doesn't reference a shared
object file.
"""
tr = Test.AddTestRun("Verify the requirement of a file")
ts = create_ts_process()
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_remap_plugin {filename}'".format(
        filename="/this/file/does/not/exist.so")
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: .*No such file or directory", "Should warn about the non-existent SO file argument")
"""
TEST: verify_remap_plugin should complain if the shared object file doesn't
have the expected Plugin symbols.
"""
tr = Test.AddTestRun("Verify the requirement of our Plugin API.")
ts = create_ts_process()
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'missing_ts_plugin_init.so'), ts)
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_remap_plugin {filename}'".format(
        filename="${PROXY_CONFIG_PLUGIN_PLUGIN_DIR}/missing_ts_plugin_init.so")
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: .*missing required function TSRemapInit", "Should warn about the need for the TSRemapInit symbol")
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR", "ERROR: .*missing required function TSRemapInit")
"""
TEST: verify_remap_plugin should complain if the plugin has the global
plugin symbols but not the remap ones.
"""
tr = Test.AddTestRun("Verify a global plugin argument produces warning.")
ts = create_ts_process()
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_hook_test.so'), ts)
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_remap_plugin {filename}'".format(
        filename="${PROXY_CONFIG_PLUGIN_PLUGIN_DIR}/ssl_hook_test.so")
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: .*missing required function TSRemapInit", "Should warn about the need for the TSRemapInit symbol")
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR", "ERROR: .*missing required function TSRemapInit")
"""
TEST: The happy case: a remap plugin shared object file is passed as an
argument that has the definition for the expected Plugin symbols.
"""
tr = Test.AddTestRun("Verify a properly formed plugin works as expected.")
ts = create_ts_process()
Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'conf_remap_stripped.so'), ts)
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_remap_plugin {filename}'".format(
        filename="${PROXY_CONFIG_PLUGIN_PLUGIN_DIR}/conf_remap_stripped.so")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "NOTE: verifying plugin '.*' Success", "Verification should succeed")
