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

import uuid

Test.Summary = '''
Test that incrementing the cache generation acts like a cache clear
'''
# need Curl
Test.SkipUnless(Condition.HasProgram("curl", "Curl need to be installed on system for this test to work"))
Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager")

# setup some config file for this server
ts.Disk.records_config.update({
    'proxy.config.body_factory.enable_customizations': 3,  # enable domain specific body factory
    'proxy.config.http.cache.generation': -1,  # Start with cache turned off
    'proxy.config.config_update_interval_ms': 1,
})
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.remap_config.AddLines([
    'map /default/ http://127.0.0.1/ @plugin=generator.so',
    # line 2
    'map /generation1/ http://127.0.0.1/' +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.cache.generation=1' +
    ' @plugin=generator.so',
    # line 3
    'map /generation2/ http://127.0.0.1/' +
    ' @plugin=conf_remap.so @pparam=proxy.config.http.cache.generation=2' +
    ' @plugin=generator.so'
])

objectid = uuid.uuid4()
# first test is a miss for default
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}/default/cache/10/{1}" -H "x-debug: x-cache,x-cache-key,via,x-cache-generation" --verbose'.format(
    ts.Variables.port, objectid)
tr.Processes.Default.ReturnCode = 0
# time delay as proxy.config.http.wait_for_cache could be broken
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=5)
tr.Processes.Default.Streams.All = "gold/miss_default-1.gold"

# Second touch is a HIT for default.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}/default/cache/10/{1}" -H "x-debug: x-cache,x-cache-key,via,x-cache-generation" --verbose'.format(
    ts.Variables.port, objectid)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = "gold/hit_default-1.gold"

# Call traffic_ctrl to set new generation
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'traffic_ctl --debug config set proxy.config.http.cache.generation 77'
tr.Processes.Default.ForceUseShell = False
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env  # set the environment for traffic_control to run in

# new generation should first be a miss.
tr = Test.AddTestRun()
tr.DelayStart = 15  # delay start of test run to allow previous command to take effect
# create a new traffic_ctrl call and the environment
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}/default/cache/10/{1}" -H "x-debug: x-cache,x-cache-key,via,x-cache-generation" --verbose'.format(
    ts.Variables.port, objectid)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = "gold/miss_default77.gold"

# new generation should should now hit.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}/default/cache/10/{1}" -H "x-debug: x-cache,x-cache-key,via,x-cache-generation" --verbose'.format(
    ts.Variables.port, objectid)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = "gold/hit_default77.gold"

# should still hit.
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl "http://127.0.0.1:{0}/default/cache/10/{1}" -H "x-debug: x-cache,x-cache-key,via,x-cache-generation" --verbose'.format(
    ts.Variables.port, objectid)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = "gold/hit_default77.gold"
