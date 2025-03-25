'''
Test the ESI plugin.
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
import re

Test.Summary = '''
Test the ESI plugin.
'''

Test.SkipUnless(Condition.PluginExists('esi.so'),)


# An enum of expected plugin behaviors for Cache-Control headers.
class CcBehaviorT():
    REMOVE_CC = 0
    MAKE_PRIVATE = 1
    PRESERVE_CC = 2


class EsiTest():
    """
    A class that encapsulates the configuration and execution of a set of EPI
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

    def __init__(self, plugin_config, cc_behavior=CcBehaviorT.REMOVE_CC):
        """
        Args:
            plugin_config (str): The base config line to place in plugin.config for
                the ATS process.
            cc_behavior (CcBehaviorT): The expected behavior of the ESI plugin
                with respect to the Cache-Control header.
        """
        if EsiTest._server is None:
            EsiTest._server = EsiTest._create_server()

        self._plugin_config = plugin_config
        self._cc_behavior = cc_behavior
        self._ts = EsiTest._create_ats(self, plugin_config)

    @staticmethod
    def _create_server():
        """
        Create and start a server process.
        """
        # Configure our server.
        server = Test.MakeOriginServer("server")

        # Generate the set of ESI responses derived right from our ESI docs.
        # See:
        #   doc/admin-guide/plugins/esi.en.rst
        request_header = {
            "headers": ("GET /esi.php HTTP/1.1\r\n"
                        "Host: www.example.com\r\n"
                        "Content-Length: 0\r\n\r\n"),
            "timestamp": "1469733493.993",
            "body": ""
        }
        esi_body = r'''<?php   header('X-Esi: 1'); ?>
<html>
<body>
Hello, <esi:include src="http://www.example.com/date.php"/>
</body>
</html>
'''
        response_header = {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "X-Esi: 1\r\n"
                    "Connection: close\r\n"
                    "Content-Length: {}\r\n"
                    "Cache-Control: max-age=300\r\n"
                    "\r\n".format(len(esi_body))),
            "timestamp": "1469733493.993",
            "body": esi_body
        }
        server.addResponse("sessionfile.log", request_header, response_header)
        request_header = {
            "headers": ("GET /date.php HTTP/1.1\r\n"
                        "Host: www.example.com\r\n"
                        "Content-Length: 0\r\n\r\n"),
            "timestamp": "1469733493.993",
            "body": ""
        }
        date_body = r'''<?php
header ("Cache-control: no-cache");
echo date('l jS \of F Y h:i:s A');
?>
'''
        response_header = {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "Content-Length: {}\r\n"
                    "Cache-Control: max-age=300\r\n"
                    "\r\n".format(len(date_body))),
            "timestamp": "1469733493.993",
            "body": date_body
        }
        server.addResponse("sessionfile.log", request_header, response_header)
        # Verify correct functionality with an empty body.
        request_header = {
            "headers": ("GET /expect_empty_body HTTP/1.1\r\n"
                        "Host: www.example.com\r\n"
                        "Content-Length: 0\r\n\r\n"),
            "timestamp": "1469733493.993",
            "body": ""
        }
        response_header = {
            "headers":
                (
                    "HTTP/1.1 200 OK\r\n"
                    "X-ESI: On\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n"
                    "\r\n"),
            "timestamp": "1469733493.993",
            "body": ""
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

        # Create a run to start the ATS process.
        tr = Test.AddTestRun("Start the ATS process.")
        tr.Processes.Default.StartBefore(ts)
        tr.Processes.Default.Command = "echo starting ATS"
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = ts
        return ts

    def _configure_client_output_expectations(self, client_process):
        client_process.Streams.stderr = "gold/esi_headers.gold"
        client_process.Streams.stdout = "gold/esi_body.gold"
        if self._cc_behavior == CcBehaviorT.REMOVE_CC:
            client_process.Streams.stderr += Testers.ExcludesExpression(
                'cache-control:', 'The Cache-Control field not be present in the response', reflags=re.IGNORECASE)
            client_process.Streams.stderr += Testers.ExcludesExpression(
                'expires:', 'The Expires field not be present in the response', reflags=re.IGNORECASE)
        if self._cc_behavior == CcBehaviorT.MAKE_PRIVATE:
            client_process.Streams.stderr += Testers.ContainsExpression(
                'cache-control:.*max-age=0, private',
                'The private response directive should be present in the response',
                reflags=re.IGNORECASE)
            client_process.Streams.stderr += Testers.ContainsExpression(
                'expires: -1', 'The Expires field should be set to -1', reflags=re.IGNORECASE)
        if self._cc_behavior == CcBehaviorT.PRESERVE_CC:
            client_process.Streams.stderr += Testers.ContainsExpression(
                'cache-control:.*max-age=300', 'The max-age directive should be present in the response', reflags=re.IGNORECASE)

    def run_cases_expecting_gzip(self):
        # Test 1: Verify basic ESI functionality.
        tr = Test.AddTestRun(f"First request for esi.php: not cached: {self._plugin_config}")
        tr.Processes.Default.Command = \
            (f'curl http://127.0.0.1:{self._ts.Variables.port}/esi.php -H"Host: www.example.com" '
             '-H"Accept: */*" --verbose')
        tr.Processes.Default.ReturnCode = 0
        self._configure_client_output_expectations(tr.Processes.Default)
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

        # Test 2: Repeat the above, should now be cached.
        tr = Test.AddTestRun(f"Second request for esi.php: will be cached: {self._plugin_config}")
        tr.Processes.Default.Command = \
            (f'curl http://127.0.0.1:{self._ts.Variables.port}/esi.php -H"Host: www.example.com" '
             '-H"Accept: */*" --verbose')
        tr.Processes.Default.ReturnCode = 0
        self._configure_client_output_expectations(tr.Processes.Default)
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

        # Test 3: Verify the ESI plugin can gzip a response when the client accepts it.
        tr = Test.AddTestRun(f"Verify the ESI plugin can gzip a response: {self._plugin_config}")
        EsiTest._output_counter += 1
        unzipped_body_file = os.path.join(tr.RunDirectory, f"non_empty_curl_output_{EsiTest._output_counter}")
        gzipped_body_file = unzipped_body_file + ".gz"
        tr.Processes.Default.Command = \
            (f'curl http://127.0.0.1:{self._ts.Variables.port}/esi.php -H"Host: www.example.com" '
             f'-H "Accept-Encoding: gzip" -H"Accept: */*" --verbose --output {gzipped_body_file}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Ready = When.FileExists(gzipped_body_file)
        tr.Processes.Default.Streams.stderr = "gold/esi_gzipped.gold"
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts
        zipped_body_disk_file = tr.Disk.File(gzipped_body_file)
        zipped_body_disk_file.Exists = True

        # Now, unzip the file and make sure its size is the expected body.
        tr = Test.AddTestRun(f"Verify the file unzips to the expected body: {self._plugin_config}")
        tr.Processes.Default.Command = f"gunzip {gzipped_body_file}"
        tr.Processes.Default.Ready = When.FileExists(unzipped_body_file)
        tr.Processes.Default.ReturnCode = 0
        unzipped_body_disk_file = tr.Disk.File(unzipped_body_file)
        unzipped_body_disk_file.Content = "gold/esi_body.gold"

        # Test 4: Verify correct handling of a gzipped empty response body.
        tr = Test.AddTestRun(f"Verify we can handle an empty response: {self._plugin_config}")
        EsiTest._output_counter += 1
        empty_body_file = os.path.join(tr.RunDirectory, f"empty_curl_output_{EsiTest._output_counter}")
        gzipped_empty_body = empty_body_file + ".gz"
        tr.Processes.Default.Command = \
             (f'curl http://127.0.0.1:{self._ts.Variables.port}/expect_empty_body '
              '-H"Host: www.example.com" -H"Accept-Encoding: gzip" -H"Accept: */*" '
              f'--verbose --output {gzipped_empty_body}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Ready = When.FileExists(gzipped_empty_body)
        tr.Processes.Default.Streams.stderr = "gold/empty_response_body.gold"
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts
        # The gzipped output file should be greater than 0, even though 0 bytes are
        # compressed.
        gz_disk_file = tr.Disk.File(gzipped_empty_body)
        gz_disk_file.Size = Testers.GreaterThan(0)

        # Now, unzip the file and make sure its size is the original 0 size body.
        tr = Test.AddTestRun(f"Verify the file unzips to a zero sized file: {self._plugin_config}")
        tr.Processes.Default.Command = f"gunzip {gzipped_empty_body}"
        tr.Processes.Default.Ready = When.FileExists(empty_body_file)
        tr.Processes.Default.ReturnCode = 0
        unzipped_disk_file = tr.Disk.File(empty_body_file)
        unzipped_disk_file.Size = 0

    def run_case_max_doc_size_too_small(self):
        tr = Test.AddTestRun(f"Max doc size too small: {self._plugin_config}")
        tr.Processes.Default.Command = \
            (f'curl http://127.0.0.1:{self._ts.Variables.port}/esi.php '
                '-H"Host: www.example.com" -H"Accept: */*" --verbose')
        tr.Processes.Default.ReturnCode = 0
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"ERROR: \[_setup\] Cannot allow attempted doc of size 121; Max allowed size is 100",
            "max doc size test should have doc size error log")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

    def run_cases_expecting_no_gzip(self):
        # Test 1: Run an ESI test where the client does not accept gzip.
        tr = Test.AddTestRun(f"First request for esi.php: gzip not accepted: {self._plugin_config}")
        tr.Processes.Default.Command = \
            (f'curl http://127.0.0.1:{self._ts.Variables.port}/esi.php '
             '-H"Host: www.example.com" -H"Accept: */*" --verbose')
        tr.Processes.Default.ReturnCode = 0
        self._configure_client_output_expectations(tr.Processes.Default)
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

        # Test 2: Verify the ESI plugin does not gzip the response even if the
        # client accepts the gzip encoding.
        tr = Test.AddTestRun(f"Verify the ESI plugin refuses to gzip responses with: {self._plugin_config}")
        tr.Processes.Default.Command = \
            (f'curl http://127.0.0.1:{self._ts.Variables.port}/esi.php '
             '-H"Host: www.example.com" -H "Accept-Encoding: gzip" -H"Accept: */*" --verbose')
        tr.Processes.Default.ReturnCode = 0
        self._configure_client_output_expectations(tr.Processes.Default)
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts


#
# Configure and run the test cases.
#

# Run the tests with ESI configured with no parameters.
vanilla_test = EsiTest(plugin_config='esi.so', cc_behavior=CcBehaviorT.REMOVE_CC)
vanilla_test.run_cases_expecting_gzip()

private_response_test = EsiTest(plugin_config='esi.so --private-response', cc_behavior=CcBehaviorT.MAKE_PRIVATE)
private_response_test.run_cases_expecting_gzip()

preserve_cc_test = EsiTest(plugin_config='esi.so --cache-control-policy 0', cc_behavior=CcBehaviorT.REMOVE_CC)
preserve_cc_test.run_cases_expecting_gzip()

preserve_cc_test = EsiTest(plugin_config='esi.so --cache-control-policy 1', cc_behavior=CcBehaviorT.MAKE_PRIVATE)
preserve_cc_test.run_cases_expecting_gzip()

preserve_cc_test = EsiTest(plugin_config='esi.so --cache-control-policy 2', cc_behavior=CcBehaviorT.PRESERVE_CC)
preserve_cc_test.run_cases_expecting_gzip()

# For these test cases, the behavior should remain the same with
# --first-byte-flush set.
first_byte_flush_test = EsiTest(plugin_config='esi.so --first-byte-flush')
first_byte_flush_test.run_cases_expecting_gzip()

# For these test cases, the behavior should remain the same with
# --packed-node-support set.
#
# Packed node support is incomplete and the following test does not work. Our
# documentation advises users not to use the --packed-node-support feature.
# This test is left here, commented out, so that it is conveniently available
# for and potential future development on this feature if desired.
#
# packed_node_support_test = EsiTest(plugin_config='esi.so --packed-node-support')
# packed_node_support_test.run_cases_expecting_gzip()

# Run a set of cases verifying that the plugin does not zip content if
# --disable-gzip-output is set.
gzip_disabled_test = EsiTest(plugin_config='esi.so --disable-gzip-output')
gzip_disabled_test.run_cases_expecting_no_gzip()

# Run the tests with too small max doc size.
max_doc_100_test = EsiTest(plugin_config='esi.so --max-doc-size 100')
max_doc_100_test.run_case_max_doc_size_too_small()

# Run the tests with no default, but sufficient, max doc size.
max_doc_2K_test = EsiTest(plugin_config='esi.so --max-doc-size 2K')
max_doc_2K_test.run_cases_expecting_gzip()
max_doc_20M_test = EsiTest(plugin_config='esi.so --max-doc-size 20M')
max_doc_20M_test.run_cases_expecting_gzip()
