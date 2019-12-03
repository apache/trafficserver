'''
Verify HTTP body buffering.
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


def int_to_hex_string(int_value):
    '''
    Convert the given int value to a hex string with no '0x' prefix.

    >>> int_to_hex_string(0)
    '0'
    >>> int_to_hex_string(1)
    '1'
    >>> int_to_hex_string(10)
    'r'
    >>> int_to_hex_string(16)
    '0'
    >>> int_to_hex_string(17)
    'f1'
    '''
    if type(int_value) != int:
        raise ValueError("Input should be an int type.")
    return hex(int_value).split('x')[1]


class BodyBufferTest:
    def __init__(cls, description):
        Test.Summary = description
        cls._origin_max_connections = 3
        cls.setupOriginServer()
        cls.setupTS()

    def setupOriginServer(self):
        self._server = Test.MakeOriginServer("server")
        self.content_length_request_body = "content-length request"
        request_header = {"headers": "POST /contentlength HTTP/1.1\r\n"
                          "Host: www.example.com\r\n"
                          "Content-Length: {}\r\n\r\n".format(
                              len(self.content_length_request_body)),
                          "timestamp": "1469733493.993",
                          "body": self.content_length_request_body}
        content_length_response_body = "content-length response"
        response_header = {"headers": "HTTP/1.1 200 OK\r\n"
                           "Server: microserver\r\n"
                           "Content-Length: {}\r\n\r\n"
                           "Connection: close\r\n\r\n".format(
                                len(content_length_response_body)),
                           "timestamp": "1469733493.993",
                           "body": content_length_response_body}
        self._server.addResponse("sessionlog.json", request_header, response_header)

        self.chunked_request_body = "chunked request"
        self.encoded_chunked_request = "{0}\r\n{1}\r\n0\r\n\r\n".format(
                int_to_hex_string(len(self.chunked_request_body)), self.chunked_request_body)
        request_header2 = {"headers": "POST /chunked HTTP/1.1\r\n"
                           "Transfer-Encoding: chunked\r\n"
                           "Host: www.example.com\r\n"
                           "Connection: keep-alive\r\n\r\n",
                           "timestamp": "1469733493.993",
                           "body": self.encoded_chunked_request}
        self.chunked_response_body = "chunked response"
        self.encoded_chunked_response = "{0}\r\n{1}\r\n0\r\n\r\n".format(
                int_to_hex_string(len(self.chunked_response_body)), self.chunked_response_body)
        response_header2 = {"headers": "HTTP/1.1 200 OK\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "Server: microserver\r\n"
                            "Connection: close\r\n\r\n",
                            "timestamp": "1469733493.993",
                            "body": self.encoded_chunked_response}
        self._server.addResponse("sessionlog.json", request_header2, response_header2)

    def setupTS(self):
        self._ts = Test.MakeATSProcess("ts", select_ports=False)
        self._ts.Disk.remap_config.AddLine(
            'map / http://127.0.0.1:{0}'.format(self._server.Variables.Port)
        )
        Test.PreparePlugin(os.path.join(Test.Variables.AtsTestToolsDir, 'plugins', 'request_buffer.c'), self._ts)
        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'request_buffer',
            'proxy.config.http.per_server.connection.max': self._origin_max_connections,
            # Disable queueing when connection reaches limit
            'proxy.config.http.per_server.connection.queue_size': 0,
        })

        self._ts.Streams.stderr = Testers.ContainsExpression(
            "request_buffer_plugin gets the request body with length\[{0}\]: \[{1}\]".format(
                len(self.content_length_request_body), self.content_length_request_body),
            "Verify that the plugin parsed the content-length request body data.")
        self._ts.Streams.stderr += Testers.ContainsExpression(
            "request_buffer_plugin gets the request body with length\[{0}\]: \[".format(
                len(self.encoded_chunked_request)),
            "Verify that the plugin parsed the chunked request body.")
        self._ts.Streams.stderr += Testers.ContainsExpression(
            "^{}".format(self.chunked_request_body),
            "Verify that the plugin parsed the chunked request body data.")

    def run(self):
        tr = Test.AddTestRun()
        # Send both a Content-Length request and a chunked-encoded request.
        tr.Processes.Default.Command = (
                'curl -v http://127.0.0.1:{0}/contentlength -d "{1}" ; '
                'curl -v http://127.0.0.1:{0}/chunked -H "Transfer-Encoding: chunked" -d "{2}"'.format(
                    self._ts.Variables.port, self.content_length_request_body, self.chunked_request_body))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(Test.Processes.ts)
        tr.Processes.Default.Streams.stderr = "200.gold"


bodyBufferTest = BodyBufferTest("Test request body buffering.")
bodyBufferTest.run()
