'''
Verify correct behavior of regex_map in remap.config.
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
Test minimum rules on load - succeed on missing file.
'''

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({f'proxy.config.url_remap.min_rules_required': 0})

tr = Test.AddTestRun("startup")
p = tr.Processes.Default
p.Command = "sh -c echo"
p.ReturnCode = 0
p.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
