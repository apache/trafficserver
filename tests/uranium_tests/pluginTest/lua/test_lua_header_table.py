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

from uranium_testkit.scenario import All, Any, Condition, Testers, UraniumTest, When


def test_lua_header_table(urtest: UraniumTest) -> None:
    '''
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

    urtest.Summary = '''
    Test lua header table functionality
    '''

    urtest.SkipUnless(Condition.PluginExists('tslua.so'),)

    urtest.ContinueOnFail = True
    # Define default ATS
    ts = urtest.MakeATSProcess("ts")

    ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1 @plugin=tslua.so @pparam=header_table.lua")
    # Configure the tslua's configuration file.
    ts.Setup.Copy("header_table.lua", ts.Variables.CONFIGDIR)

    # Test - Check for header table
    tr = urtest.AddTestRun("Lua Header Table")
    ps = tr.Processes.Default  # alias
    ps.StartBefore(urtest.Processes.ts)
    tr.MakeCurlCommand(f"-s -D /dev/stderr -H 'X-Test: test1' -H 'X-Test: test2' http://127.0.0.1:{ts.Variables.port}", ts=ts)
    ps.Env = ts.Env
    ps.ReturnCode = 0
    ps.Streams.stdout.Content = Testers.ContainsExpression("test1test2", "expected header table results")
    tr.StillRunningAfter = ts
    urtest.execute()
