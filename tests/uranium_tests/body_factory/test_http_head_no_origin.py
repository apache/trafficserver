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


def test_http_head_no_origin(urtest: UraniumTest) -> None:
    '''
    Tests that HEAD requests return proper responses when origin fails
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
    import sys

    urtest.Summary = '''
    Tests that HEAD requests return proper responses when origin fails
    '''

    ts = urtest.MakeATSProcess("ts")
    server = urtest.MakeOriginServer("server")

    HOST = 'www.example.test'

    urtest.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
    urtest.Setup.Copy('data')

    tr = urtest.AddTestRun("Test domain {0}".format(HOST))
    tr.Processes.Default.StartBefore(urtest.Processes.ts)
    tr.StillRunningAfter = ts

    tr.Processes.Default.Command = f"{sys.executable} tcp_client.py 127.0.0.1 {ts.Variables.port} data/{HOST}_head.txt"
    tr.Processes.Default.TimeOut = 5  # seconds
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = "gold/http-head-no-origin.gold"
    urtest.execute()
