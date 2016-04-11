'''
Test the configure entry : proxy.config.http.origin_min_keep_alive_connections
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

import time
import logging
import uuid
import socket
import requests
import tsqa.test_cases
import helpers
import SocketServer

log = logging.getLogger(__name__)


class KAHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which return chunked encoding optionally

    /parts/sleep_time/close
        parts: number of parts to send
        sleep_time: time between parts
        close: bool wether to close properly
    """

    def handle(self):
        # Receive the data in small chunks and retransmit it
        conn_id = uuid.uuid4().hex
        start = time.time()
        while True:
            data = self.request.recv(4096).strip()
            if data:
                log.info('Sending data back to the client: {uid}'.format(uid=conn_id))
            else:
                log.info('Client disconnected: {timeout}seconds'.format(timeout=time.time() - start))
                break
            body = conn_id
            time.sleep(1)
            resp = ('HTTP/1.1 200 OK\r\n'
                    'Content-Length: {content_length}\r\n'
                    'Content-Type: text/html; charset=UTF-8\r\n'
                    'Connection: keep-alive\r\n'
                    '\r\n'
                    '{body}'.format(content_length=len(body), body=body))
            self.request.sendall(resp)


class TestKeepAlive_Origin_Min_connections(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.traffic_server_host = '127.0.0.1'
        cls.traffic_server_port = int(cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        cls.socket_server_port = int(tsqa.utils.bind_unused_port()[1])
        log.info("socket_server_port = %d" % (cls.socket_server_port))
        cls.server = tsqa.endpoint.SocketServerDaemon(KAHandler, port=cls.socket_server_port)
        cls.server.start()
        cls.server.ready.wait()

        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.socket_server_port))
        cls.origin_keep_alive_timeout = 1

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.origin_min_keep_alive_connections':  1,
            'proxy.config.http.keep_alive_enabled_out': 1,
            'proxy.config.http.keep_alive_no_activity_timeout_out': cls.origin_keep_alive_timeout,
            'proxy.config.exec_thread.limit': 1,
            'proxy.config.exec_thread.autoconfig': 0,
        })

    def test_origin_min_connection(self):
        response_uuids = []
        # make the request N times, ensure that they are on the same connection
        for _ in xrange(0, 3):
            ret = requests.get('http://{0}:{1}/'.format(self.traffic_server_host, self.traffic_server_port))
            response_uuids.append(ret.text)

        self.assertEqual(1, len(set(response_uuids)))

        # sleep for a time greater than the keepalive timeout and ensure its the same connection
        time.sleep(self.origin_keep_alive_timeout * 2)
        ret = requests.get('http://{0}:{1}/'.format(self.traffic_server_host, self.traffic_server_port))
        self.assertEqual(ret.text, response_uuids[0])
