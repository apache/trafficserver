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

import requests
import logging
import random, string

import tsqa.test_cases
import tsqa.utils
import tsqa.endpoint
import os

origin_content_length = 0
log = logging.getLogger(__name__)

#Test positive cases of remap gzip plugin
gzip_remap_bench = [
             # Test gzip
            { "args": "@pparam=gzip1.config",
              "files": [("gzip1.config", "enabled true\nremove-accept-encoding true\ncache false\ncompressible-content-type text/*\n")
                       ],
            },
            { "args": "@pparam=gzip2.config",
              "files": [("gzip2.config", "enabled true\nremove-accept-encoding false\ncache false\ncompressible-content-type text/*\n")
                       ],
            },
            { "args": "@pparam=gzip3.config",
              "files": [("gzip3.config", "enabled true\nremove-accept-encoding true\ncache true\ncompressible-content-type text/*\n")
                       ],
            },
            { "args": "@pparam=gzip4.config",
              "files": [("gzip4.config", "enabled true\nremove-accept-encoding true\ncache true\ncompressible-content-type text/*\nflush true\n")
                       ],
            },
            { "args": "@pparam=gzip5.config",
              "files": [("gzip5.config", "enabled true\nremove-accept-encoding true\ncache true\ncompressible-content-type text/*\nflush false\n")
                       ],
            },
            ]

#Test negative cases of remap gzip plugin
gzip_remap_negative_bench =  [
            #Test when gzip is disabled
            { "args": "@pparam=gzip_negative1.config",
              "files": [("gzip_negative1.config", "enabled false\nremove-accept-encoding true\ncache false\ncompressible-content-type text/*\n")
                        ],
            },
            #Test when compressible content doesn't match
            { "args": "@pparam=gzip_negative2.config",
              "files": [("gzip_negative2.config", "enabled true\nremove-accept-encoding true\ncache false\ncompressible-content-type !text/*\n")
                        ],
            },
            #Test when disallow is configured to match some pattern
            { "args": "@pparam=gzip_negative3.config",
              "files": [("gzip_negative3.config", "enabled true\nremove-accept-encoding true\ncache false\ncompressible-content-type text/*\ndisallow *test*\n")
                        ],
            },
            ]

#Test global gzip plugin
gzip_global_bench = [
            { "args": "gzip_global1.config",
              "files": [("gzip_global1.config", "enabled true\nremove-accept-encoding true\ncache true\ncompressible-content-type text/*\n")
                       ],
            },
            ]

#Set up an origin server which returns random string.
def handler(request):
    global origin_content_length
    rand_string = ''.join(random.choice(string.lowercase) for i in range(500))
    origin_content_length = len(rand_string)
    return rand_string

def create_config_files(env, test):
    # Create gzip config files.
    for file in test['files']:
        filename = file[0]
        content = file[1]
        path = os.path.join(env.layout.prefix, 'etc/trafficserver', filename);
        with open(path, 'w') as fh:
            fh.write(content)

class StaticEnvironmentCase(tsqa.test_cases.EnvironmentCase):
    @classmethod
    def getEnv(cls):
        #layout = tsqa.environment.Layout('/opt/gitlab-gzip')
        layout = tsqa.environment.Layout('/opt/apache/trafficserver.TS-4147')
        env = tsqa.environment.Environment()
        env.clone(layout=layout)
        return env

#Test gzip remap plugin
class TestGzipRemapPlugin(tsqa.test_cases.DynamicHTTPEndpointCase, StaticEnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['plugin.config'].add_line('xdebug.so')
        cls.configs['records.config']['CONFIG'].update({
           'proxy.config.diags.debug.enabled': 1,
           'proxy.config.diags.debug.tags': '.*',
           'proxy.config.diags.debug.tags': 'gzip.*',
           'proxy.config.url_remap.pristine_host_hdr': 1,})

        cls.http_endpoint.add_handler('/path/to/object', handler)

        def add_remap_rule(remap_prefix, remap_index, test):
            host = 'test_{0}_{1}.example.com'.format(remap_prefix, remap_index)
            port = cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
            args = test['args']
            remap_rule = 'map http://{0}:{1} http://127.0.0.1:{2} @plugin=gzip.so {3}'.format(host, port, cls.http_endpoint.address[1], args)
            log.info('  {0}'.format(remap_rule))
            cls.configs['remap.config'].add_line(remap_rule)

        # Prepare gzip tests related remap rules.
        i = 0
        for test in gzip_remap_bench:
            add_remap_rule("gzip", i, test)
            create_config_files(env, test)
            i+=1

        #Prepare negative gzip tests related remap rules.
        i = 0
        for test in gzip_remap_negative_bench:
            add_remap_rule("gzip_negative", i, test)
            create_config_files(env, test)
            i+=1

    def send_request(self,remap_prefix, remap_index):
        host = 'test_{0}_{1}.example.com'.format( remap_prefix, remap_index)
        port = self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
        url = 'http://127.0.0.1:{0}/path/to/object'.format(port)
        log.info('host is {0}, port is {1}, url is {2}'.format(host, port, url))
        s = requests.Session()
        s.headers.update({'Host': '{0}:{1}'.format(host, port)})
        s.headers.update({'Accept-Encoding:': 'gzip'})
        response = s.get(url)
        log.info('Response headers obtained: {0}'.format(response.headers))
        return response

    def send_gzip_request(self, remap_prefix, remap_index):
        '''
        Sends a gzip request to the traffic server
        '''
        response = self.send_request(remap_prefix, remap_index)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.headers['Content-Encoding'], 'gzip')
        self.assertLess(int(response.headers['Content-Length']), int(origin_content_length))

    def send_gzip_request_negative(self, remap_prefix, remap_index):
        '''
        Sends a gzip request to the traffic server
        '''
        response = self.send_request(remap_prefix, remap_index)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(int(response.headers['Content-Length']), int(origin_content_length))

    def test_gzip_remap_plugin(self):
        i = 0
        for test in gzip_remap_bench:
            self.send_gzip_request('gzip', i)
            i += 1

        i = 0
        for test in gzip_remap_negative_bench:
            self.send_gzip_request_negative('gzip_negative', i)
            i += 1

#Test gzip global plugin
class TestGzipGlobalPlugin(tsqa.test_cases.DynamicHTTPEndpointCase, StaticEnvironmentCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['plugin.config'].add_line('xdebug.so')

        cls.configs['records.config']['CONFIG'].update({
           'proxy.config.diags.debug.enabled': 1,
           'proxy.config.diags.debug.tags': 'gzip.*',
           'proxy.config.url_remap.pristine_host_hdr': 1,})

        cls.http_endpoint.add_handler('/path/to/object', handler)

        def add_remap_rule(remap_prefix, remap_index):
            host = 'test_{0}_{1}.example.com'.format(remap_prefix, remap_index)
            port = cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
            remap_rule = 'map http://{0}:{1} http://127.0.0.1:{2}'.format(host, port, cls.http_endpoint.address[1])
            log.info('  {0}'.format(remap_rule))
            cls.configs['remap.config'].add_line(remap_rule)

        def add_global_plugin_rule(test):
            args = test['args']
            plugin_rule = 'gzip.so {0}'.format(args)
            log.info('  {0}'.format(plugin_rule))
            cls.configs['plugin.config'].add_line(plugin_rule)

        # Prepare gzip plugin rules
        i = 0
        for test in gzip_global_bench:
            add_remap_rule("gzip_global",i)
            add_global_plugin_rule(test)
            create_config_files(env, test)
            i+=1

    def send_request(self,remap_prefix, remap_index):
        host = 'test_{0}_{1}.example.com'.format( remap_prefix, remap_index)
        port = self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
        url = 'http://127.0.0.1:{0}/path/to/object'.format(port)
        log.info('host is {0}, port is {1}, url is {2}'.format(host, port, url))
        s = requests.Session()
        s.headers.update({'Host': '{0}:{1}'.format(host, port)})
        s.headers.update({'Accept-Encoding:': 'gzip'})
        response = s.get(url)
        log.info('Response headers obtained: {0}'.format(response.headers))
        return response

    def send_global_gzip_request(self, remap_prefix, remap_index):
        '''
        Sends a gzip request to the traffic server
        '''
        response = self.send_request(remap_prefix, remap_index)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.headers['Content-Encoding'], 'gzip')
        self.assertLess(int(response.headers['Content-Length']), int(origin_content_length))

    def test_gzip_global_plugin(self):
        i = 0
        for test in gzip_global_bench:
            self.send_global_gzip_request("gzip_global", i)
            i += 1

