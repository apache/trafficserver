'''
Test custom log field
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

log = logging.getLogger(__name__)


class TestCustomLogField(helpers.EnvironmentCase):
    '''
    Tests for a customed log field called hii
    '''
    @classmethod
    def setUpEnv(cls, env):

        cls.configs['remap.config'].add_line(
            'map / http://www.linkedin.com/ @action=deny'
        )
        cls.log_file_name = 'test_log_field'
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.log.custom_logs_enabled': 1,
        })

        cls.log_file_path = os.path.join(cls.environment.layout.prefix, 'var/log/test_log_field.log')
        cls.log_etc_file = os.path.join(cls.environment.layout.prefix, 'etc/trafficserver/logs_xml.config')
        cls.configs['logs_xml.config'].add_line('<LogFormat><Name = "testlogfield"/><Format = "%<hii> %<hiih>"/></LogFormat>')
        cls.configs['logs_xml.config'].add_line('<LogObject><Format = "testlogfield"/><Filename = "test_log_field"/><Mode = "ascii"/></LogObject>')

    def ip_to_hex(self, ipstr):
      num_list = ipstr.split('.')
      int_value = (int(num_list[0]) << 24) + (int(num_list[1]) << 16) + (int(num_list[2]) << 8) + (int(num_list[3]))
      return hex(int_value).upper()[2:]

    def test_log_field(self):
      random.seed()
      times = 10
      for i in xrange(times):
        request_ip = "127.%d.%d.%d" % (random.randint(1, 255), random.randint(1, 255), random.randint(1, 255))
        url = 'http://%s:%s' % (request_ip, self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
        requests.get(url)
        # get the last line of the log file
        time.sleep(10)
        with open(self.log_file_path) as f:
          for line in f:
            pass
        expected_line = "%s %s\n" % (request_ip, self.ip_to_hex(request_ip))
        self.assertEqual(line, expected_line)
