'''
Verify the cache freshness limit and current age log fields.
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

Test.Summary = 'Verify cache freshness limit and current age log fields'

traffic_run = Test.ATSReplayTest(replay_file='replay/cache-freshness-fields.replay.yaml')
ts = traffic_run.Processes.ts
log_path = os.path.join(ts.Variables.LOGDIR, 'cache_freshness_fields.log')

Test.AddAwaitFileContainsTestRun(
    'Wait for cache freshness log output',
    log_path,
    r'^uncacheable ',
)

validation_run = Test.AddTestRun('Validate cache freshness log fields')
validation_script = os.path.join(Test.TestDirectory, 'verify_cache_freshness_fields.py')
validation_run.Processes.Default.Command = f'{sys.executable} {validation_script} {log_path}'
validation_run.Processes.Default.ReturnCode = 0
