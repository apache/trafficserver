'''
Verify the behavior of proxy.config.http.per_server.connection.min.
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

Test.Summary = __doc__

replay_run = Test.ATSReplayTest(replay_file='minimum_keep_alive.replay.yaml')
checker = replay_run.Processes.Process('connection-count')
checker.Command = 'sleep 5; traffic_ctl metric get proxy.process.http.current_server_connections'
checker.ReturnCode = 0
checker.Env = replay_run.Processes.ts.Env
checker.Streams.stdout = Testers.ContainsExpression(
    r'^proxy\.process\.http\.current_server_connections\s+1$', 'The origin connection pool should retain exactly one connection.')
checker.StartBefore(replay_run.Processes.ts)
