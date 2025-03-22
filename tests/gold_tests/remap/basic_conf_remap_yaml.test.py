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

Test.Summary = '''
Test conf_remap using a yaml file.
'''

Test.ContinueOnFail = True


class conf_remap_yaml_load_test:
    """Test conf_remap using a yaml file."""

    client_counter: int = 0
    ts_counter: int = 0
    server_counter: int = 0

    def __init__(self, name: str, gold_file="", remap_filename="", remap_content=""):
        """Initialize the test.
        :param name: The name of the test.
        :param gold_file: Gold file to be checked.
        :param remap_filename: Remap yaml filename.
        :param remap_content: remap yaml file content.
        """
        self.name = name
        self.gold_file = gold_file
        self._remap_filename = remap_filename
        self._remap_content = remap_content

    def _configure_server(self, tr: 'TestRun'):
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        server = Test.MakeOriginServer(f"server-{conf_remap_yaml_load_test.ts_counter}", lookup_key="{%Host}{PATH}")
        request_header2 = {
            "headers": "GET /test HTTP/1.1\r\nHost: www.testexample.com\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        response_header2 = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

        server.addResponse("sessionfile.log", request_header2, response_header2)
        conf_remap_yaml_load_test.server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun'):
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the ts process with.
        """
        ts = Test.MakeATSProcess(f"ts-{conf_remap_yaml_load_test.ts_counter}")

        conf_remap_yaml_load_test.ts_counter += 1
        ts.Disk.records_config.update(
            '''
            diags:
                debug:
                    enabled: 1
                    tags: conf_remap
            dns:
                resolv_conf: NULL
            http:
                referer_filter: 1
            url_remap:
                pristine_host_hdr: 0 # make sure is 0

        ''')
        self._ts = ts

    def run(self, diags_fail_exp="", ts_retcode=0):
        """Run the test.
        :param diags_fail_exp: Text to be included to validate the error.
        :param ts_retcode: Expected return code from TS.
        """
        tr = Test.AddTestRun(self.name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)

        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        self._ts.ReturnCode = ts_retcode

        if ts_retcode > 0:  # we could have errors logged and yet, we still want to move on.
            self._ts.Ready = 0

        if diags_fail_exp != "":
            # some error logs will be written to the diags.
            self._ts.Disk.diags_log.Content = Testers.IncludesExpression(diags_fail_exp, "Have a look.")
        else:
            tr.Processes.Default.ReturnCode = 0

        if self.gold_file:
            tr.Processes.Default.Streams.stderr = self.gold_file

        if self._remap_filename != "" and self._remap_content != "":
            self._ts.Disk.MakeConfigFile(self._remap_filename).update(self._remap_content)
            self._ts.Disk.remap_config.AddLine(
                f'map http://www.testexample.com/ http://127.0.0.1:{self._server.Variables.Port} @plugin=conf_remap.so @pparam={self._remap_filename}'
            )

        tr.CurlCommand(
            '--proxy 127.0.0.1:{0} "http://www.testexample.com/test" -H "Host: www.testexample.com" --verbose'.format(
                self._ts.Variables.port))
        conf_remap_yaml_load_test.client_counter += 1


test0 = conf_remap_yaml_load_test(
    "Test success",
    gold_file="gold/200OK_test.gold",
    remap_filename="testexample_remap.yaml",
    remap_content='''
    records:
      url_remap:
        pristine_host_hdr: 1
    ''')
test0.run()

test1 = conf_remap_yaml_load_test(
    "Test mismatch type",
    remap_filename="mismatch_field_type_remap.yaml",
    remap_content='''
    records:
      url_remap:
        pristine_host_hdr: !!float '1'
    ''')
test1.run(diags_fail_exp="'proxy.config.url_remap.pristine_host_hdr' variable type mismatch", ts_retcode=33)

test2 = conf_remap_yaml_load_test(
    "Test invalid variable",
    remap_filename="invalid1_field_type_remap.yaml",
    remap_content='''
    records:
      plugin:
        dynamic_reload_mode: 1
    ''')

test2.run(
    diags_fail_exp="'proxy.config.plugin.dynamic_reload_mode' is not a configuration variable or cannot be overridden",
    ts_retcode=33)

# We let the conf_remap parse two fields, only one is valid, we expect ATS to start and the invalid fields ignored.
test3 = conf_remap_yaml_load_test(
    "Test success",
    gold_file="gold/200OK_test.gold",
    remap_filename="testexample2_remap.yaml",
    remap_content='''
    records:
      plugin:
        dynamic_reload_mode: 1

      url_remap:
        pristine_host_hdr: 1
    ''')
test3.run(diags_fail_exp="'proxy.config.plugin.dynamic_reload_mode' is not a configuration variable or cannot be overridden")

# Check null values
test4 = conf_remap_yaml_load_test(
    "Test success - with NULL variable",
    gold_file="gold/200OK_test.gold",
    remap_filename="testexample_remap.yaml",
    remap_content='''
    records:
      url_remap:
        pristine_host_hdr: 1
      hostdb:
        ip_resolve: "NULL" # We want to make sure this gets read as it should. "NULL" could be the value of this field.
    ''')
test4.run()
