'''
Test that traffic_cache_tool reports an exception escaping main and exits with 1.
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
Test that traffic_cache_tool exits with 1 instead of aborting when --aos is not a number.
'''

# std::stoi throws std::invalid_argument on a non-numeric --aos value. main()
# must report it and return 1 rather than let it reach std::terminate, which
# would abort with SIGABRT.
tr = Test.AddTestRun("Non-numeric --aos is reported, not aborted on")
tr.Processes.Default.Command = "$ATS_BIN/traffic_cache_tool --aos abc list"
tr.Processes.Default.ReturnCode = 1
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
    r"traffic_cache_tool: terminating on an unhandled std::\S*invalid_argument",
    "Expect the exception type to be reported on stderr.")
