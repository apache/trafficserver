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


def test_copy_config2(urtest: UraniumTest) -> None:
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

    urtest.Summary = "Test start up of Traffic server with generated ports of more than one servers at the same time"

    # set up some ATS processes
    ts1 = urtest.MakeATSProcess("ts1")
    ts2 = urtest.MakeATSProcess("ts2")

    # setup a testrun
    t = urtest.AddTestRun("Talk to ts1")
    t.StillRunningAfter = ts1
    t.StillRunningAfter += ts2
    p = t.Processes.Default
    t.MakeCurlCommand("127.0.0.1:{0}".format(ts1.Variables.port), ts=ts1)
    p.ReturnCode = 0
    p.StartBefore(urtest.Processes.ts1)
    p.StartBefore(urtest.Processes.ts2)

    # setup a testrun
    t = urtest.AddTestRun("Talk to ts2")
    t.StillRunningBefore = ts1
    t.StillRunningBefore += ts2
    t.StillRunningAfter = ts1
    t.StillRunningAfter += ts2
    p = t.Processes.Default
    t.MakeCurlCommand("127.0.0.1:{0}".format(ts2.Variables.port), ts=ts2)
    p.ReturnCode = 0
    urtest.execute()
