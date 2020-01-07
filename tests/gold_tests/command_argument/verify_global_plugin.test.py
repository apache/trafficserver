'''
Test the verify_global_plugin TrafficServer command.
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
Test that the TrafficServer verify_global_plugin command works as expected.
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
TEST: verify_global_plugin should complain if an argument is not passed to it.
"""
tr = Test.AddTestRun("Verify the requirement of an argument")
ts = create_ts_process()
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = "traffic_server -C 'verify_global_plugin'"
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: verifying a plugin requires a plugin SO file path argument",
    "Should warn about the need for an SO file argument")


"""
TEST: verify_global_plugin should complain if the argument doesn't reference a shared
object file.
"""
tr = Test.AddTestRun("Verify the requirement of a file")
ts = create_ts_process()
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_global_plugin {filename}'".format(
        filename="/this/file/does/not/exist.so")
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: .*No such file or directory",
    "Should warn about the non-existent SO file argument")


"""
TEST: verify_global_plugin should complain if the shared object file doesn't
have the expected Plugin symbols.
"""
tr = Test.AddTestRun("Verify the requirement of our Plugin API.")
ts = create_ts_process()
Test.PreparePlugin(
    os.path.join(Test.Variables.AtsTestToolsDir,
                 'plugins', 'missing_ts_plugin_init.cc'),
    ts)
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_global_plugin {filename}'".format(
        filename="${PROXY_CONFIG_PLUGIN_PLUGIN_DIR}/missing_ts_plugin_init.so")
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: .*unable to find TSPluginInit function in",
    "Should warn about the need for the TSPluginInit symbol")
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR",
    "ERROR: .*unable to find TSPluginInit function in")


"""
TEST: Verify that passing a remap plugin produces a warning because
it doesn't have the global plugin symbols.
"""
tr = Test.AddTestRun("Verify a properly formed plugin works as expected.")
ts = create_ts_process()
Test.PreparePlugin(
    os.path.join(Test.Variables.AtsTestToolsDir,
                 'plugins', 'conf_remap_stripped.cc'),
    ts)
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_global_plugin {filename}'".format(
        filename="${PROXY_CONFIG_PLUGIN_PLUGIN_DIR}/conf_remap_stripped.so")
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: .*unable to find TSPluginInit function in",
    "Should warn about the need for the TSPluginInit symbol")
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR",
    "ERROR: .*unable to find TSPluginInit function in")


"""
TEST: The happy case: a global plugin shared object file is passed as an
argument that has the definition for the expected Plugin symbols.
"""
tr = Test.AddTestRun("Verify a properly formed plugin works as expected.")
ts = create_ts_process()
Test.PreparePlugin(
    os.path.join(Test.Variables.AtsTestToolsDir,
                 'plugins', 'ssl_hook_test.cc'),
    ts)
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_global_plugin {filename}'".format(
        filename="${PROXY_CONFIG_PLUGIN_PLUGIN_DIR}/ssl_hook_test.so")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "NOTE: verifying plugin '.*' Success",
    "Verification should succeed")


def prepare_undefined_symbol_plugin(tsproc, path_c, path_cpp, path_h):
    """
    Intentionally create an SO file with an undefined symbol.

    We've seen issues where a plugin is created in which a C++ file
    includes a function declaration and then expects a definition
    of the mangled version of that function. However, the definition
    was created with a c-compiler and thus is not mangled. This
    builds a plugin with just such an undefined mangled symbol.
    """
    plugin_dir = tsproc.Env['PROXY_CONFIG_PLUGIN_PLUGIN_DIR']
    tsproc.Setup.Copy(path_c, plugin_dir)
    tsproc.Setup.Copy(path_cpp, plugin_dir)
    tsproc.Setup.Copy(path_h, plugin_dir)

    in_basename = os.path.basename(path_c)
    out_basename = os.path.splitext(in_basename)[0] + '.so'
    out_path = os.path.join(plugin_dir, out_basename)
    tsproc.Setup.RunCommand(
        ("gcc -c -fPIC {path_c} -o {path_c}_o; "
            "g++ -c -fPIC {path_cpp} -o {path_cpp}_o; "
            "g++ {path_c}_o {path_cpp}_o -shared -o {out_path}").format(
                **locals())
    )


"""
TEST: This is a regression test for a shared object file that doesn't have all
of the required symbols defined because of a malformed interaction between C
and C++ files.
"""
tr = Test.AddTestRun("Regression test for an undefined, mangled C++ symbol.")
ts = create_ts_process()
plugins_dir = os.path.join(Test.Variables.AtsTestToolsDir, 'plugins')
prepare_undefined_symbol_plugin(
    ts,
    os.path.join(plugins_dir, 'missing_mangled_definition.c'),
    os.path.join(plugins_dir, 'missing_mangled_definition.cc'),
    os.path.join(plugins_dir, 'missing_mangled_definition.h'))
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = \
    "traffic_server -C 'verify_global_plugin {filename}'".format(
        filename="${PROXY_CONFIG_PLUGIN_PLUGIN_DIR}/missing_mangled_definition.so")
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    "ERROR: .*: undefined symbol: .*foo.*",
    "Should warn about the need for the TSPluginInit symbol")
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR",
    "ERROR: .*: undefined symbol: .*foo.*")
