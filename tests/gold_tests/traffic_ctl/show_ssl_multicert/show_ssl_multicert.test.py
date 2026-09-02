'''
Test the traffic_ctl config ssl-multicert show command.
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

Test.Summary = 'Test traffic_ctl config ssl-multicert show command.'


class ShowSSLMulticert:

    def __init__(self):
        self.setup_ts()
        self.setup_show_default()
        self.setup_show_json()
        self.setup_show_yaml()

    def setup_ts(self):
        self._ts = Test.MakeATSProcess("ts", enable_cache=False, enable_tls=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - ssl_cert_name: server.pem
    dest_ip: "*"
    ssl_key_name: server.key
""".split("\n"))
        self._ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{self._ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{self._ts.Variables.SSLDir}',
            })

    def setup_show_default(self):
        tr = Test.AddTestRun("Test ssl-multicert show (default YAML format)")
        tr.Processes.Default.Command = 'traffic_ctl config ssl-multicert show'
        tr.Processes.Default.Streams.stdout = "gold/show_yaml.gold"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

    def setup_show_json(self):
        # Test with explicit --json flag.
        tr = Test.AddTestRun("Test ssl-multicert show --json")
        tr.Processes.Default.Command = 'traffic_ctl config ssl-multicert show --json'
        tr.Processes.Default.Streams.stdout = "gold/show_json.gold"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts

        # Test with short -j flag.
        tr = Test.AddTestRun("Test ssl-multicert show -j")
        tr.Processes.Default.Command = 'traffic_ctl config ssl-multicert show -j'
        tr.Processes.Default.Streams.stdout = "gold/show_json.gold"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts

    def setup_show_yaml(self):
        # Test with --yaml flag.
        tr = Test.AddTestRun("Test ssl-multicert show --yaml")
        tr.Processes.Default.Command = 'traffic_ctl config ssl-multicert show --yaml'
        tr.Processes.Default.Streams.stdout = "gold/show_yaml.gold"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts

        # Test with short -y flag.
        tr = Test.AddTestRun("Test ssl-multicert show -y")
        tr.Processes.Default.Command = 'traffic_ctl config ssl-multicert show -y'
        tr.Processes.Default.Streams.stdout = "gold/show_yaml.gold"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts


ShowSSLMulticert()
