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

Test.Summary = 'Test loading storage metrics'
Test.ContinueOnFail = True

# CAVEAT: Below test cases doesn't have multiple span cases that requires RAW devices.
test_cases = [
    {
        "case": 0,
        "description": "default config",
        "storage": '''
cache:
  spans:
    - id: disk.0
      path: storage
      size: 256M
'''
    },
    {
        "case": 1,
        "description": "four equally devided volumes",
        "storage":
            '''
cache:
  spans:
    - id: disk.0
      path: storage
      size: 1G
  volumes:
    - id: 1
      size: 25%
    - id: 2
      size: 25%
    - id: 3
      size: 25%
    - id: 4
      size: 25%
'''
    },
    {
        "case": 2,
        "description": "exclusive span",
        "storage":
            '''
cache:
  spans:
    - id: disk.0
      path: storage
      size: 256M
  volumes:
    - id: 1
      spans:
        - use: disk.0
          size: 100%
''',
        "hosting": '''
hostname=* volume=1
'''
    },
    {
        "case": 3,
        "description": "two equally devided volumes without size option",
        "storage": '''
cache:
  spans:
    - id: disk.0
      path: storage
      size: 512M
  volumes:
    - id: 1
    - id: 2
'''
    },
]


class StorageMetricsTest:
    """
    Test loading storage.yaml

    1. Spawn TS process with configs in test_cases
    2. Get 'proxy.process.cache.*' metrics
    3. Check with 'gold/storage_N_stdout.gold' file
    """

    def run(self):
        for config in test_cases:
            i = config["case"]
            ts = Test.MakeATSProcess(f"ts_{i}")
            ts.Disk.storage_yaml.AddLine(config["storage"])

            if "hosting" in config:
                ts.Disk.hosting_config.AddLine(config["hosting"])

            ts.Disk.records_config.update({
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'cache',
            })

            tr = Test.AddTestRun()

            tr.Processes.Default.StartBefore(ts)
            tr.Processes.Default.Env = ts.Env
            tr.Processes.Default.Command = 'traffic_ctl --debug metric match proxy.process.cache.'
            tr.Processes.Default.ReturnCode = 0
            tr.Processes.Default.Streams.stdout = f"gold/storage_metrics_{i}_stdout.gold"
            tr.StillRunningAfter = ts


StorageMetricsTest().run()
