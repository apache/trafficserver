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

import os

Test.Summary = 'Test loading volume.yaml'
Test.ContinueOnFail = True


class VolumeYamlTest:
    """Test that loading volume.yaml"""

    def __init__(self):
        self.setupTS()

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts")
        self.ts.Disk.volume_config.AddLines(
            '''
volumes:
  - volume: 1
    scheme: http
    size: 50%
  - volume: 2
    scheme: http
    size: 50%
'''.split('\n'))

        self.ts.Disk.storage_config.AddLines(
            '''
storage 1G
'''.split('\n'))

    def checkVolumeMetrics(self):
        tr = Test.AddTestRun("Check volume metrics")

        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Command = 'traffic_ctl --debug metric match proxy.process.cache.volume'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.Streams.stdout = "gold/volume_yaml_0_stdout.gold"
        tr.StillRunningAfter = self.ts

    def run(self):
        self.checkVolumeMetrics()


VolumeYamlTest().run()
