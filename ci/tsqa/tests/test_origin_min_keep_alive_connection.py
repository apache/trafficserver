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

import os
import requests
import time
import logging
import SocketServer
import uuid
import socket
import tsqa.test_cases
import helpers
import thread

log = logging.getLogger(__name__)

def simple_socket_server(host, port):
    log.info("starting the socket server")
    serv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    serv.bind((host, port))
    serv.setblocking(1)
    serv.listen(3)
    conn_id = uuid.uuid4().hex
    while True:
        conn, addr = serv.accept()
        data = conn.recv(4096).strip()
        if data:
            log.info('Sending data back to the client: {uid}'.format(uid=conn_id))
        else:
            log.info('Client disconnected: {timeout}seconds'.format(timeout=now))
            break
        body = conn_id
        resp = ('HTTP/1.1 200 OK\r\n'
                'Content-Length: {content_length}\r\n'
                'Content-Type: text/html; charset=UTF-8\r\n'
                'Connection: keep-alive\r\n'
                '\r\n'
                '{body}'.format(content_length=len(body), body=body))
        conn.sendall(resp)
    serv.shutdown(socket.SHUT_RDWR)
    serv.close()
    log.info("end the socket server")


class TestKeepAlive_Origin_Min_connections(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.traffic_server_host = '127.0.0.1'
        cls.traffic_server_port = int(cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        cls.socket_server_port = int(tsqa.utils.bind_unused_port()[1])
        log.info("socket_server_port = %d" % (cls.socket_server_port))
        thread.start_new_thread(simple_socket_server, (cls.traffic_server_host, cls.socket_server_port, ))
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.socket_server_port))
        cls.origin_keep_alive_timeout = 3
        cls.configs['records.config']['CONFIG']['origin_min_keep_alive_connections'] = 1 
        cls.configs['records.config']['CONFIG']['keep_alive_enabled_out'] = 1 
        cls.configs['records.config']['CONFIG']['proxy.config.http.keep_alive_no_activity_timeout_out'] = cls.origin_keep_alive_timeout
    
    def test_origin_min_connection(self):
        conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        conn.connect((self.traffic_server_host, self.traffic_server_port))
        request_content = 'GET / HTTP/1.1\r\nConnection: keep-alive\r\nHost: 127.0.0.1\r\n\r\n'
        conn.setblocking(1)
        conn.send(request_content)
        first_resp = None
        second_resp = None
        while 1:
            try:
                resp = conn.recv(4096)
                resp = resp.split('\r\n\r\n')[1]
                log.info(resp)
                if first_resp == None:
                    first_resp = resp
                else:
                    second_resp = resp
                    break
                if len(resp) == 0:  
                    break
                time.sleep(2 + self.origin_keep_alive_timeout)
                conn.send(request_content)
            except:
                break
        conn.shutdown(socket.SHUT_RDWR)
        conn.close()
        self.assertEqual(first_resp, second_resp)
    
