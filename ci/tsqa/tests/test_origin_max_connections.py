'''
Test the configure entry : proxy.config.http.origin_max_connections
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
import thread
from multiprocessing import Pool
import SocketServer

log = logging.getLogger(__name__)


# TODO: seems like a useful shared class- either add to httpbin or some shared lib
class KAHandler(SocketServer.BaseRequestHandler):
    '''SocketServer that returns the connection-id  as the body
    '''
    # class variable to set number of active sessions
    alive_sessions = 0

    def handle(self):
        KAHandler.alive_sessions += 1
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
                    'X-Current-Sessions: {alive_sessions}\r\n'
                    '\r\n'
                    '{body}'.format(content_length=len(body), alive_sessions=KAHandler.alive_sessions, body=body))
            self.request.sendall(resp)
        KAHandler.alive_sessions -= 1


class TestKeepAlive_Origin_Max_connections(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.traffic_server_host = '127.0.0.1'
        cls.traffic_server_port = int(cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        cls.socket_server_port = int(tsqa.utils.bind_unused_port()[1])

        log.info("socket_server_port = %d" % (cls.socket_server_port))
        cls.server = tsqa.endpoint.SocketServerDaemon(KAHandler, port=cls.socket_server_port)
        cls.server.start()
        cls.server.ready.wait()

        cls.socket_server_port2 = int(tsqa.utils.bind_unused_port()[1])
        cls.server2 = tsqa.endpoint.SocketServerDaemon(KAHandler, port=cls.socket_server_port2)
        cls.server2.start()
        cls.server2.ready.wait()

        cls.configs['remap.config'].add_line('map /other/ http://127.0.0.1:{0}'.format(cls.socket_server_port2))
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.socket_server_port))

        cls.origin_keep_alive_timeout = 1

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.origin_max_connections':  1,
            'proxy.config.http.keep_alive_enabled_out': 1,
            'proxy.config.http.keep_alive_no_activity_timeout_out': cls.origin_keep_alive_timeout,
            'proxy.config.exec_thread.limit': 2,
            'proxy.config.exec_thread.autoconfig': 0,
        })


    def test_max(self):
        '''
        '''
        REQUEST_COUNT = 8
        url = 'http://{0}:{1}/'.format(self.traffic_server_host, self.traffic_server_port)
        url2 = 'http://{0}:{1}/other/'.format(self.traffic_server_host, self.traffic_server_port)
        results = []
        results2 = []
        pool = Pool(processes=4)
        for _ in xrange(0, REQUEST_COUNT):
            results.append(pool.apply_async(requests.get, (url,)))
            results2.append(pool.apply_async(requests.get, (url2,)))

        # TS-4340
        # ensure that the 2 origins (2 different ports on loopback) were running in parallel
        for i in xrange(0, REQUEST_COUNT):
            self.assertEqual(int(results[i].get().headers['X-Current-Sessions']), 2)
            self.assertEqual(int(results2[i].get().headers['X-Current-Sessions']), 2)
