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

Test.Summary = '''
Test the Multiplexer plugin.
'''

Test.SkipUnless(
    Condition.PluginExists('multiplexer.so')
)


class MultiplexerTestBase:
    """
    Encapsulates the base configuration used by each test.
    """

    client_counter = 0
    server_counter = 0
    ts_counter = 0

    def __init__(self, replay_file, multiplexed_host_replay_file, skip_post):
        self.replay_file = replay_file
        self.multiplexed_host_replay_file = multiplexed_host_replay_file

        self.setupServers()
        self.setupTS(skip_post)

    def setupServers(self):
        counter = MultiplexerTestBase.server_counter
        MultiplexerTestBase.server_counter += 1
        self.server_origin = Test.MakeVerifierServerProcess(
            f"server_origin_{counter}", self.replay_file)
        self.server_http = Test.MakeVerifierServerProcess(
            f"server_http_{counter}", self.multiplexed_host_replay_file)
        self.server_https = Test.MakeVerifierServerProcess(
            f"server_https_{counter}", self.multiplexed_host_replay_file)

        # The origin should never receive "X-Multiplexer: copy"
        self.server_origin.Streams.All += Testers.ExcludesExpression(
            'X-Multiplexer: copy',
            'Verify the original server target never receives a "copy".')

        # Nor should the multiplexed hosts receive an "original" X-Multiplexer value.
        self.server_http.Streams.All += Testers.ExcludesExpression(
            'X-Multiplexer: original',
            'Verify the HTTP multiplexed host does not receive an "original".')
        self.server_https.Streams.All += Testers.ExcludesExpression(
            'X-Multiplexer: original',
            'Verify the HTTPS multiplexed host does not receive an "original".')

        # In addition, the original server should always receive the POST and
        # PUT requests.
        self.server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: POST',
            "Verify the client's original target received the POST transaction.")
        self.server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: PUT',
            "Verify the client's original target received the PUT transaction.")

        # Under all configurations, the GET request should be multiplexed.
        self.server_origin.Streams.All += Testers.ContainsExpression(
            'X-Multiplexer: original',
            'Verify the client\'s original target received the "original" request.')
        self.server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET',
            "Verify the client's original target received the GET request.")

        self.server_http.Streams.All += Testers.ContainsExpression(
            'X-Multiplexer: copy',
            'Verify the HTTP server received a "copy" of the request.')
        self.server_http.Streams.All += Testers.ContainsExpression(
            'uuid: GET',
            "Verify the HTTP server received the GET request.")

        self.server_https.Streams.All += Testers.ContainsExpression(
            'X-Multiplexer: copy',
            'Verify the HTTPS server received a "copy" of the request.')
        self.server_https.Streams.All += Testers.ContainsExpression(
            'uuid: GET',
            "Verify the HTTPS server received the GET request.")

        # Verify that the HTTPS server receives a TLS connection.
        self.server_https.Streams.All += Testers.ContainsExpression(
            'Finished accept using TLSSession',
            "Verify the HTTPS was indeed used by the HTTPS server.")

    def setupTS(self, skip_post):
        counter = MultiplexerTestBase.ts_counter
        MultiplexerTestBase.ts_counter += 1
        self.ts = Test.MakeATSProcess(f"ts_{counter}", enable_tls=True, enable_cache=False)
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.records_config.update({
            "proxy.config.ssl.server.cert.path": f'{self.ts.Variables.SSLDir}',
            "proxy.config.ssl.server.private_key.path": f'{self.ts.Variables.SSLDir}',
            "proxy.config.ssl.client.verify.server.policy": 'PERMISSIVE',
            'proxy.config.ssl.keylog_file': '/tmp/tls_session_keys.txt',

            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'multiplexer',
        })
        self.ts.Disk.ssl_multicert_config.AddLine(
            'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
        )
        skip_remap_param = ''
        if skip_post:
            skip_remap_param = ' @pparam=proxy.config.multiplexer.skip_post_put=1'
        self.ts.Disk.remap_config.AddLines([
            f'map https://origin.server.com https://127.0.0.1:{self.server_origin.Variables.https_port} '
            f'@plugin=multiplexer.so @pparam=nontls.server.com @pparam=tls.server.com'
            f'{skip_remap_param}',

            # Now create remap entries for the multiplexed hosts: one that
            # verifies HTTP, and another that verifies HTTPS.
            f'map http://nontls.server.com http://127.0.0.1:{self.server_http.Variables.http_port}',
            f'map http://tls.server.com https://127.0.0.1:{self.server_https.Variables.https_port}',
        ])

    def run(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.server_origin)
        tr.Processes.Default.StartBefore(self.server_http)
        tr.Processes.Default.StartBefore(self.server_https)
        tr.Processes.Default.StartBefore(self.ts)

        counter = MultiplexerTestBase.client_counter
        MultiplexerTestBase.client_counter += 1
        tr.AddVerifierClientProcess(
            f"client_{counter}",
            self.replay_file,
            https_ports=[self.ts.Variables.ssl_port])


class MultiplexerTest(MultiplexerTestBase):
    """
    Exercise multiplexing without skip_post configuration.
    """

    replay_file = os.path.join("replays", "multiplexer_original.replay.yaml")
    multiplexed_host_replay_file = os.path.join("replays", "multiplexer_copy.replay.yaml")

    def __init__(self):
        super().__init__(
            MultiplexerTest.replay_file,
            MultiplexerTest.multiplexed_host_replay_file,
            skip_post=False)

    def setupServers(self):
        super().setupServers()

        # Both of the multiplexed hosts should receive the POST because skip_post
        # is disabled.
        self.server_http.Streams.All += Testers.ContainsExpression(
            'uuid: POST',
            "Verify the HTTP server received the POST request.")
        self.server_https.Streams.All += Testers.ContainsExpression(
            'uuid: POST',
            "Verify the HTTPS server received the POST request.")

        # Same with PUT
        self.server_http.Streams.All += Testers.ContainsExpression(
            'uuid: PUT',
            "Verify the HTTP server received the PUT request.")
        self.server_https.Streams.All += Testers.ContainsExpression(
            'uuid: PUT',
            "Verify the HTTPS server received the PUT request.")


class MultiplexerSkipPostTest(MultiplexerTestBase):
    """
    Exercise multiplexing with skip_post configuration.
    """

    replay_file = os.path.join("replays", "multiplexer_original_skip_post.replay.yaml")
    multiplexed_host_replay_file = os.path.join("replays", "multiplexer_copy_skip_post.replay.yaml")

    def __init__(self):
        super().__init__(
            MultiplexerSkipPostTest.replay_file,
            MultiplexerSkipPostTest.multiplexed_host_replay_file,
            skip_post=True)

    def setupServers(self):
        super().setupServers()

        # Neither of the multiplexed hosts should receive the POST because skip_post
        # is enabled.
        self.server_http.Streams.All += Testers.ExcludesExpression(
            'uuid: POST',
            "Verify the HTTP server did not receive the POST request.")
        self.server_https.Streams.All += Testers.ExcludesExpression(
            'uuid: POST',
            "Verify the HTTPS server did not receive the POST request.")

        # Same with PUT.
        self.server_http.Streams.All += Testers.ExcludesExpression(
            'uuid: PUT',
            "Verify the HTTP server did not receive the PUT request.")
        self.server_https.Streams.All += Testers.ExcludesExpression(
            'uuid: PUT',
            "Verify the HTTPS server did not receive the PUT request.")


MultiplexerTest().run()
MultiplexerSkipPostTest().run()
