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
import os

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
            if 'timeout' in data:
                print 'sleep for a long time!'
                time.sleep(4)
            else:
                time.sleep(2)
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

        queue_path = os.path.join(cls.environment.layout.sysconfdir, 'queue.conf')
        with open(queue_path, 'w') as fh:
            fh.write('CONFIG proxy.config.http.origin_max_connections_queue INT 2')

        noqueue_path = os.path.join(cls.environment.layout.sysconfdir, 'noqueue.conf')
        with open(noqueue_path, 'w') as fh:
            fh.write('CONFIG proxy.config.http.origin_max_connections_queue INT 0')

        cls.configs['remap.config'].add_line('map /other/queue/ http://127.0.0.1:{0} @plugin=conf_remap.so @pparam={1}'.format(cls.socket_server_port2, queue_path))
        cls.configs['remap.config'].add_line('map /other/noqueue/ http://127.0.0.1:{0} @plugin=conf_remap.so @pparam={1}'.format(cls.socket_server_port2, noqueue_path))
        cls.configs['remap.config'].add_line('map /other/ http://127.0.0.1:{0}'.format(cls.socket_server_port2))
        cls.configs['remap.config'].add_line('map /queue/ http://127.0.0.1:{0} @plugin=conf_remap.so @pparam={1}'.format(cls.socket_server_port, queue_path))
        cls.configs['remap.config'].add_line('map /noqueue/ http://127.0.0.1:{0} @plugin=conf_remap.so @pparam={1}'.format(cls.socket_server_port, noqueue_path))
        cls.configs['remap.config'].add_line('map / http://127.0.0.1:{0}'.format(cls.socket_server_port))

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.origin_max_connections':  1,
            'proxy.config.http.keep_alive_enabled_out': 1,
            'proxy.config.http.keep_alive_no_activity_timeout_out': 1,
            'proxy.config.http.transaction_active_timeout_out': 2,
            'proxy.config.http.connect_attempts_timeout': 2,
            'proxy.config.http.connect_attempts_rr_retries': 0,
            'proxy.config.exec_thread.limit': 1,
            'proxy.config.exec_thread.autoconfig': 0,
        })

    def _send_requests(self, total_requests, path='', other=False):
        url = 'http://{0}:{1}/{2}'.format(self.traffic_server_host, self.traffic_server_port, path)
        url2 = 'http://{0}:{1}/other/{2}'.format(self.traffic_server_host, self.traffic_server_port, path)
        jobs = []
        jobs2 = []
        pool = Pool(processes=4)
        for _ in xrange(0, total_requests):
            jobs.append(pool.apply_async(requests.get, (url,)))
            if other:
                jobs2.append(pool.apply_async(requests.get, (url2,)))

        results = []
        results2 = []
        for j in jobs:
            try:
                results.append(j.get())
            except Exception as e:
                results.append(e)

        for j in jobs2:
            try:
                results2.append(j.get())
            except Exception as e:
                results2.append(e)

        return results, results2


    # TODO: enable after TS-4340 is merged
    # and re-enable `other` for the remaining queueing tests
    def tesst_origin_scoping(self):
        '''Send 2 requests to loopback (on separate ports) and ensure that they run in parallel
        '''
        results, results2 = self._send_requests(1, other=True)

        # TS-4340
        # ensure that the 2 origins (2 different ports on loopback) were running in parallel
        for i in xrange(0, REQUEST_COUNT):
            self.assertEqual(int(results[i].get().headers['X-Current-Sessions']), 2)
            self.assertEqual(int(results2[i].get().headers['X-Current-Sessions']), 2)

    def test_origin_default_queueing(self):
        '''By default we have no queue limit
        '''
        REQUEST_COUNT = 4
        results, results2 = self._send_requests(REQUEST_COUNT)

        for x in xrange(0, REQUEST_COUNT):
            self.assertEqual(results[x].status_code, 200)
            #self.assertEqual(results2[x].status_code, 200)

    def test_origin_queueing(self):
        '''If a queue is set, N requests are queued and the rest immediately fail
        '''
        REQUEST_COUNT = 4
        results, results2 = self._send_requests(REQUEST_COUNT, path='queue/')

        success = 0
        fail = 0
        for x in xrange(0, REQUEST_COUNT):
            if results[x].status_code == 200:
                success += 1
            else:
                fail += 1
        self.assertEqual(success, 3)

    def test_origin_queueing_timeouts(self):
        '''Lets have some requests timeout and ensure that the queue is freed up
        '''
        REQUEST_COUNT = 4
        results, results2 = self._send_requests(REQUEST_COUNT, path='queue/timeout')

        success = 0
        fail = 0
        for x in xrange(0, REQUEST_COUNT):
            if results[x].status_code == 200:
                success += 1
                print 'success', x
            else:
                fail += 1
        self.assertEqual(fail, 4)

        self.test_origin_queueing()

    def test_origin_no_queueing(self):
        '''If the queue is set to 0, all requests past the max immediately fail
        '''
        REQUEST_COUNT = 4
        results, results2 = self._send_requests(REQUEST_COUNT, path='noqueue/')

        success = 0
        fail = 0
        for x in xrange(0, REQUEST_COUNT):
            if results[x].status_code == 200:
                success += 1
            else:
                fail += 1
        print 'results:', success, fail
        self.assertEqual(success, 1)
