'''
Test header_rewrite with URL conditions and operators.
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
Test header_rewrite with URL conditions and operators.
'''


class TestHeaderRewriteURL:
    '''Test header_rewrite with URL conditions and operators.'''

    _replay_file = 'replay/header_rewrite_url.replay.yaml'

    def __init__(self):
        self._configure_server()
        self._configure_traffic_server()
        self._configure_client()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server.
        :return: The server process.
        '''
        server = Test.MakeVerifierServerProcess("server", self._replay_file)
        self._server = server
        self._server_port = server.Variables.http_port
        server.Streams.All += Testers.ContainsExpression(
            'GET //prepend/this/original/path', 'Verify the prepended path from set_destination_prefix.conf.')
        server.Streams.All += Testers.ContainsExpression(
            'GET /prepend/this/original/path', 'Verify the prepended path from set_destination.conf.')
        return server

    def _configure_traffic_server(self):
        '''Configure Traffic Server.
        :return: The ATS process.
        '''
        ts = Test.MakeATSProcess("ts", enable_cache=False)
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.show_location': 0,
                'proxy.config.diags.debug.tags': 'http|header.*',
            })
        ts.Setup.CopyAs('rules/rule_client.conf', Test.RunDirectory)
        ts.Setup.CopyAs('rules/set_redirect.conf', Test.RunDirectory)
        ts.Setup.CopyAs('rules/set_destination.conf', Test.RunDirectory)
        ts.Setup.CopyAs('rules/set_destination_prefix.conf', Test.RunDirectory)

        # This configuration makes use of CLIENT-URL in conditions.
        ts.Disk.remap_config.AddLine(
            f'map http://www.url.condition.com/ http://127.0.0.1:{self._server_port}/to_path/ '
            f'@plugin=header_rewrite.so @pparam={Test.RunDirectory}/rule_client.conf')
        # This configuration makes use of the set-destination operator with a '/' prefix.
        ts.Disk.remap_config.AddLine(
            f'map http://www.set.destination.prefix.com/ http://127.0.0.1:{self._server_port}/ '
            f'@plugin=header_rewrite.so @pparam={Test.RunDirectory}/set_destination_prefix.conf')
        # This configuration makes use of the set-destination operator without a '/' prefix.
        ts.Disk.remap_config.AddLine(
            f'map http://www.set.destination.com/ http://127.0.0.1:{self._server_port}/ '
            f'@plugin=header_rewrite.so @pparam={Test.RunDirectory}/set_destination.conf')
        # This configuration makes use of TO-URL in a set-redirect operator.
        ts.Disk.remap_config.AddLine(
            'map http://no_path.com http://no_path.com?name=brian/ '
            f'@plugin=header_rewrite.so @pparam={Test.RunDirectory}/set_redirect.conf')
        self._ts = ts
        return ts

    def _configure_client(self):
        '''Configure the client and the TestRun.'''
        tr = Test.AddTestRun('Test header_rewrite with URL conditions and operators')
        p = tr.AddVerifierClientProcess(
            "client", self._replay_file, http_ports=[self._ts.Variables.port], other_args='--thread-limit 1')
        self._ts.StartBefore(self._server)
        p.StartBefore(self._ts)
        p.Streams.All += Testers.ContainsExpression('200 OK', 'Verify that a 200 OK was received for the destination prefix tests.')
        p.Streams.All += Testers.ContainsExpression('304', 'Verify that a 304 was received for the rule_client.conf.')
        p.Streams.All += Testers.ContainsExpression('301', 'Verify that a 301 was received for the set_redirect.conf.')


TestHeaderRewriteURL()
