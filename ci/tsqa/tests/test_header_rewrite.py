'''
Test cookie rewrite
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
import random
import tsqa.test_cases
import helpers
import shutil
import SocketServer
import urllib2

log = logging.getLogger(__name__)

class EchoServerHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which will return all data received back
    """

    def handle(self):
        # Receive the data in small chunks and retransmit it
        while True:
            data = self.request.recv(4096).strip()
            if data:
                log.debug('Sending data back to the client')
            else:
                log.debug('Client disconnected')
                break
            cookie = ''
            if 'Cookie' in data:
                cookie = data.split('Cookie: ')[1].split('\r\n')[0]

            resp = ('HTTP/1.1 200 OK\r\n'
                    'Content-Length: {data_length}\r\n'
                    'Content-Type: text/html; charset=UTF-8\r\n'
                    'Connection: keep-alive\r\n'
                    '\r\n{data_string}'.format(
                        data_length = len(cookie),
                        data_string = cookie
                    ))
            self.request.sendall(resp)

class TestHeaderRewrite(helpers.EnvironmentCase):
    '''
    Tests for header rewrite
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.traffic_server_port = int(cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])

        # create a socket server
        cls.socket_server = tsqa.endpoint.SocketServerDaemon(EchoServerHandler)
        cls.socket_server.start()
        cls.socket_server.ready.wait()

        cls.configs['remap.config'].add_line(
            'map / http://127.0.0.1:%d' %(cls.socket_server.port)
        )

        # setup the plugin
        cls.config_file = 'header-rewrite.config'
        cls.test_config_path = helpers.tests_file_path(cls.config_file)

        cls.configs['plugin.config'].add_line('%s/header_rewrite.so %s' % (
          cls.environment.layout.plugindir,
          cls.test_config_path
        ))

    def test_cookie_rewrite(self):

        cookie_test_add_dict = {
          '' : 'testkey=testaddvalue',
          'testkey=somevalue' : 'testkey=somevalue',
          'otherkey=testvalue' : 'otherkey=testvalue;testkey=testaddvalue',
          'testkey = "other=value"; a = a' : 'testkey = "other=value"; a = a',
          'testkeyx===' : 'testkeyx===;testkey=testaddvalue'
        }
        for key in cookie_test_add_dict:
            opener = urllib2.build_opener()
            opener.addheaders.append(('Cookie', key))
            f = opener.open("http://127.0.0.1:%d/addcookie" % (self.traffic_server_port))
            resp = f.read()
            self.assertEqual(resp, cookie_test_add_dict[key])

        cookie_test_rm_dict = {
          '' : '',
          '  testkey=somevalue' : '',
          'otherkey=testvalue' : 'otherkey=testvalue',
          'testkey = "other=value" ; a = a' : ' a = a',
          'otherkey=othervalue= ; testkey===' : 'otherkey=othervalue= ',
          'firstkey ="firstvalue" ; testkey = =; secondkey=\'\'' : 'firstkey ="firstvalue" ;  secondkey=\'\''
        }
        for key in cookie_test_rm_dict:
            opener = urllib2.build_opener()
            opener.addheaders.append(('Cookie', key))
            f = opener.open("http://127.0.0.1:%d/rmcookie" % (self.traffic_server_port))
            resp = f.read()
            self.assertEqual(resp, cookie_test_rm_dict[key])

        cookie_test_set_dict = {
          '' : 'testkey=testsetvalue',
          'testkey=somevalue' : 'testkey=testsetvalue',
          'otherkey=testvalue' : 'otherkey=testvalue;testkey=testsetvalue',
          'testkey = "other=value"; a = a' : 'testkey = testsetvalue; a = a',
          'testkeyx===' : 'testkeyx===;testkey=testsetvalue',
          'firstkey ="firstvalue" ; testkey = =; secondkey=\'\'' : 'firstkey ="firstvalue" ; testkey = testsetvalue; secondkey=\'\''
        }
        for key in cookie_test_set_dict:
            opener = urllib2.build_opener()
            opener.addheaders.append(('Cookie', key))
            f = opener.open("http://127.0.0.1:%d/setcookie" % (self.traffic_server_port))
            resp = f.read()
            self.assertEqual(resp, cookie_test_set_dict[key])
