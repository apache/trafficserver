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
import socket
import tsqa
import tsqa.test_cases
import tsqa.utils
import thread
import SocketServer
import time
import requests
unittest = tsqa.utils.import_unittest()
import helpers
import threading
import logging

log = logging.getLogger(__name__)

class AtomicCounter:
    def __init__(self):
        self.lock = threading.Lock()
        self.counter = 0

    def inc(self, increment = 1):
        self.lock.acquire()
        self.counter += increment
        self.lock.release()

    def get(self):
        value = 0
        self.lock.acquire()
        value = self.counter
        self.lock.release()
        return value

    def reset(self):
        self.lock.acquire()
        self.counter = 0
        self.lock.release()

data_receive_times = AtomicCounter()

class EchoReceivedLengthHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler that sends back how many bytes it has received
    """

    def handle(self):
        chunked = False
        global data_receive_times

        while True:
            data = self.request.recv(65536)
            log.info("server receive data length: %d" % (len(data)))
            if len(data) > 0 and len(data) < 200:
              log.info("server receive data: %s" % (data))
            if not data:
                log.info('Client disconnected')
                break
            if 'Transfer-Encoding: chunked' in data:
              chunked = True

            data_receive_times.inc();
            if not chunked or '0\r\n\r\n' in data:
              log.info('Sending data back to the client')
              resp_str = str(len(data))
              resp = ('HTTP/1.1 200 OK\r\n'
                      'Content-Length: %d\r\n'
                      'Content-Type: text/html; charset=UTF-8\r\n'
                      'Connection: keep-alive\r\n'
                      '\r\n%s' %(
                        len(resp_str),
                        resp_str
                      ))
              self.request.sendall(resp)

            if chunked and '0\r\n\r\n' in data:
              log.info('Client disconnected')
              break

        chunked = False

class TestRequestBuffer(helpers.EnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.traffic_server_host = '127.0.0.1'
        cls.traffic_server_port = int(cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        cls.configs['records.config']['CONFIG']['proxy.config.http.chunking_enabled'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.enabled'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.tags'] = 'http.*'
        # create a socket server
        cls.port = tsqa.utils.bind_unused_port()[1]
        cls.socket_server = tsqa.endpoint.SocketServerDaemon(EchoReceivedLengthHandler, port=cls.port)
        cls.socket_server.start()
        cls.socket_server.ready.wait()
        log.info(cls.environment.layout.logdir)
        log.info("socket_server_port = %d, cls.traffic_server_port= %d" % (cls.socket_server.port, cls.traffic_server_port))
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.socket_server.port))
        cls.configs['plugin.config'].add_line('%s/RequestBufferPlugin.so' %(cls.environment.layout.plugindir))

    def test_request_buffer_content_length_0(self):
        """
        test for sending post header with content length 0
        """
        global data_receive_times
        data_receive_times.reset()
        small_post_headers = {'Content-Length' : 0}
        ret = requests.post(
            'http://127.0.0.1:%d' % (self.traffic_server_port),
            headers = small_post_headers
        )

        self.assertEqual(data_receive_times.get(), 1)
        self.assertEqual(ret.status_code, 200)


    def test_request_buffer_content_length_small(self):
        """
        test for sending post request all in once
        """
        global data_receive_times
        data_receive_times.reset()

        req = 'POST / HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length:2\r\nHost: 127.0.0.1\r\n\r\nab'
        conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        conn.connect((self.traffic_server_host, self.traffic_server_port))
        conn.setblocking(1)
        conn.send(req)
        resp = conn.recv(4096)

        # data_receive_times will be 1 or 2. Request header and body may send separately
        self.assertEqual(data_receive_times.get() == 1 or data_receive_times.get() == 2, True)
        self.assertIn('HTTP/1.1 200 OK', resp)

    def test_request_buffer_content_length_large(self):
        """
        test for sending large post
        """
        global data_receive_times
        data_receive_times.reset()

        str_length = 2000
        send_times = 30
        content_str = ''
        for i in xrange(0, str_length):
          content_str = content_str + 'a'

        conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        conn.connect((self.traffic_server_host, self.traffic_server_port))

        hdr = 'POST / HTTP/1.1\r\nConnection: keep-alive\r\nContent-Length:%d\r\nHost: 127.0.0.1\r\n\r\n' % (str_length * send_times)
        conn.setblocking(1)
        conn.send(hdr)
        for i in xrange(0, send_times):
          time.sleep(0.1)
          self.assertEqual(data_receive_times.get(), 0)
          conn.send(content_str)

        log.info("recv_times = %d, send_times = %d" % (data_receive_times.get(), send_times))
        resp = conn.recv(4096)
        self.assertIn('HTTP/1.1 200 OK', resp)

    def test_request_buffer_chunked_small(self):
        """
        test for sending post request with chunked encoding all in once
        """
        global data_receive_times
        data_receive_times.reset()

        conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        conn.connect((self.traffic_server_host, self.traffic_server_port))
        req = 'POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n2\r\n12\r\n0\r\n\r\n'
        conn.setblocking(1)
        conn.send(req)

        resp = conn.recv(4096)

        log.info("recv_times = %d, send_times = 1" % (data_receive_times.get()))
        # data_receive_times will be 1 or 2. Request header and body may send separately
        self.assertEqual(data_receive_times.get() == 1 or data_receive_times.get() == 2, True)
        self.assertIn('HTTP/1.1 200 OK', resp)

    def test_request_buffer_chunked_large(self):
        """
        test for sending large post data with chunked encoding
        """
        global data_receive_times
        data_receive_times.reset()
        str_length = 2000
        chunked_size = hex(str_length)[2:]
        send_times = 30
        content_str = ''
        for i in xrange(0, str_length):
          content_str = content_str + 'a'

        conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        conn.connect((self.traffic_server_host, self.traffic_server_port))
        hdr = 'POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n'
        conn.setblocking(1)
        conn.send(hdr)

        for i in xrange(0, send_times):
          time.sleep(0.1)
          self.assertEqual(data_receive_times.get(), 0)
          conn.send('%s\r\n%s\r\n' %(chunked_size, content_str))

        conn.send('0\r\n\r\n')
        log.info("recv_times = %d, send_times = %d" % (data_receive_times.get(), send_times))
        resp = conn.recv(4096)
        self.assertIn('HTTP/1.1 200 OK', resp)
