'''
Test nested include for the ESI plugin.
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

Test.Summary = '''
Test nested include for the ESI plugin.
'''

Test.SkipUnless(Condition.PluginExists('esi.so'),)


class EsiTest():
    """
    A class that encapsulates the configuration and execution of a set of ESI
    test cases.
    """
    """ static: The same server Process is used across all tests. """
    _server = None
    """ static: A counter to keep the ATS process names unique across tests. """
    _ts_counter = 0
    """ static: A counter to keep any output file names unique across tests. """
    _output_counter = 0
    """ The ATS process for this set of test cases. """
    _ts = None

    def __init__(self, plugin_config):
        """
        Args:
            plugin_config (str): The config line to place in plugin.config for
                the ATS process.
        """
        if EsiTest._server is None:
            EsiTest._server = EsiTest._create_server()

        self._ts = EsiTest._create_ats(self, plugin_config)

    @staticmethod
    def _create_server():
        """
        Create and start a server process.
        """
        # Configure our server.
        server = Test.MakeOriginServer("server", lookup_key="{%uuid}")

        # Generate the set of ESI responses.
        request_header = {
            "headers":
                "GET /esi-nested-include.php HTTP/1.1\r\n" + "Host: www.example.com\r\n" + "Content-Length: 0\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        esi_body = r'''<p>
<esi:include src="http://www.example.com/esi-nested-include.html"/>
</p>
'''
        response_header = {
            "headers":
                "HTTP/1.1 200 OK\r\n" + "X-Esi: 1\r\n" + "Cache-Control: private\r\n" +
                "Content-Type: text/html\r\n" + "Connection: close\r\n" + "Content-Length: {}\r\n".format(len(esi_body)) + "\r\n",
            "timestamp": "1469733493.993",
            "body": esi_body
        }
        server.addResponse("sessionfile.log", request_header, response_header)

        # Create a run to start the server.
        tr = Test.AddTestRun("Start the server.")
        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.Command = "echo starting the server"
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = server

        return server

    @staticmethod
    def _create_ats(self, plugin_config):
        """
        Create and start an ATS process.
        """
        EsiTest._ts_counter += 1

        # Configure ATS with a vanilla ESI plugin configuration.
        ts = Test.MakeATSProcess("ts{}".format(EsiTest._ts_counter))
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|plugin_esi',
        })
        ts.Disk.remap_config.AddLine(f'map http://www.example.com/ http://127.0.0.1:{EsiTest._server.Variables.Port}')
        ts.Disk.plugin_config.AddLine(plugin_config)

        ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r'The current esi inclusion depth \(3\) is larger than or equal to the max \(3\)',
            'Verify the ESI error concerning the max inclusion depth')

        # Create a run to start the ATS process.
        tr = Test.AddTestRun("Start the ATS process.")
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Command = "echo starting ATS"
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = ts
        return ts

    def run_test(self):
        # Test 1: Verify basic ESI functionality without processing internal txn.
        tr = Test.AddTestRun("First request")
        tr.MakeCurlCommand(
            f'http://127.0.0.1:{self._ts.Variables.port}/main.php -H"Host: www.example.com" '
            '-H"Accept: */*" --verbose')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/nested_include_body.gold"
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts


#
# Configure and run the test cases.
#

# Run the tests with ESI configured with private response.
first_test = EsiTest(plugin_config='esi.so')
first_test.run_test()
