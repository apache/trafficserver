'''
Shared library code for ATS autest tests. Add and update this file as needed.
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

import glob
import os
import time


class ATSTestManager:
    """
    A comprehensive test management class for Apache Traffic Server tests.
    """
    host_example = "Host: www.example.com"
    conn_keepalive = "Connection: keep-alive"

    def __init__(self, Test, When, test_name="ts"):
        """
        Initialize the ATSTestManager with ATS and origin server processes.
        """
        self.Test = Test
        self.When = When
        self.ts = self.Test.MakeATSProcess(test_name)
        self.server = self.Test.MakeOriginServer("server")

        self.ats_port = self.ts.Variables.port
        self.origin_port = self.server.Variables.Port
        self.run_dir = self.Test.RunDirectory
        self.localhost = f"127.0.0.1:{self.ats_port}"

        self.started = False

        # Default response header template
        self.default_response_header = {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }

    def enable_diagnostics(self, tags="http_hdrs"):
        """
        Configure debug logging for the ATS process.
        """
        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.show_location': 0,
                'proxy.config.diags.debug.tags': tags,
            })

    def add_server_responses(self, responses, log_file="sessionfile.log"):
        """
        Add responses to the origin server for the given request/response pairs.
        """
        for req, resp in responses:
            if resp is None:
                resp = self.default_response_header
            self.server.addResponse(log_file, req, resp)

    def add_remap_rules(self, remap_rules, disable_tls=True):
        """
        Add remap rules to the ATS configuration.

        This method processes a list of remap rule dictionaries and adds them to the
        remap.config file. Each rule can have associated plugins with parameters.
        """
        for rule in remap_rules:
            plugin_args = self._build_plugin_args(rule["plugins"])

            self.ts.Disk.remap_config.AddLine(f'map http://{rule["from"]} http://{rule["to"]} {plugin_args}')
            if not disable_tls:
                self.ts.Disk.remap_config.AddLine(f'map https://{rule["from"]} http://{rule["to"]} {plugin_args}')

    def _build_plugin_args(self, plugins):
        """
        Build plugin arguments string from plugins list.
        """
        if not plugins:
            return ""

        return " ".join(f'@plugin={plugin}.so ' + " ".join(f'@pparam={p}' for p in params) for plugin, params in plugins)

    def execute_tests(self, test_runs):
        """
        Execute all configured test runs.
        """
        for test in test_runs:
            if not 'desc' in test:
                raise ValueError("Test run must include 'desc' key")
            tr = self.Test.AddTestRun(test["desc"])

            # Start processes before the first test
            if not self.started:
                tr.Processes.Default.StartBefore(self.server, ready=self.When.PortOpen(self.origin_port))
                tr.Processes.Default.StartBefore(self.ts)
                self.started = True

            if curl := test.get("curl"):
                tr.MakeCurlCommand(curl, ts=self.ts)
            elif multi := test.get("multi_curl"):
                tr.MakeCurlCommandMulti(multi, ts=self.ts)

            if gold := test.get("gold"):
                tr.Processes.Default.Streams.stderr = gold

            tr.StillRunningAfter = self.server

    def set_traffic_out_content(self, content_file):
        """
        Set the traffic.out content file for debugging.

        Args:
            content_file (str): Path to the content file
        """
        self.ts.Disk.traffic_out.Content = content_file

    def copy_files(self, source, pattern, destination_dir=None):
        """
        Copy files using either glob pattern matching or explicit file list.

        This method supports two modes:
        1. Pattern matching: When source is a string directory path, it finds all files
           matching the glob pattern and copies them to the destination.
        2. Explicit file list: When source is a list of file paths, it copies each
           file directly (pattern parameter is ignored in this case).
        """
        if destination_dir is None:
            destination_dir = self.run_dir

        if isinstance(source, list):
            for file_path in source:
                self.ts.Setup.CopyAs(file_path, destination_dir)
        elif pattern is not None:
            for file_path in glob.glob(os.path.join(self.Test.TestDirectory, source, pattern)):
                relative_path = os.path.relpath(file_path, self.Test.TestDirectory)
                self.ts.Setup.CopyAs(relative_path, destination_dir)
        else:
            raise ValueError("Either 'source' must be a list of files or 'pattern' must be provided with a source directory.")
