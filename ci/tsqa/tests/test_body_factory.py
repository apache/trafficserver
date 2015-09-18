'''
Test body_factory
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
import logging
import random
import tsqa.test_cases
import helpers

log = logging.getLogger(__name__)


class TestDomainSpecificBodyFactory(helpers.EnvironmentCase):
    '''
    Tests for how body factory works with requests of different domains
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.body_factory.enable_customizations': 3,  # enable domain specific body factory
        })
        cls.configs['remap.config'].add_line(
            'map / http://www.linkedin.com/ @action=deny'
        )
        cls.body_factory_dir = os.path.join(cls.environment.layout.prefix, cls.configs['records.config']['CONFIG']['proxy.config.body_factory.template_sets_dir'])
        cls.domain_directory = ['www.linkedin.com', '127.0.0.1', 'www.foobar.net']
        for directory_item in cls.domain_directory:
            current_dir = os.path.join(cls.body_factory_dir, directory_item)
            try:
                os.mkdir(current_dir)
            except:
                pass
            fname = os.path.join(current_dir, "access#denied")
            with open(fname, "w") as f:
              f.write(directory_item)
            fname = os.path.join(current_dir, ".body_factory_info")
            with open(fname, "w") as f:
              pass

    def test_domain_specific_body_factory(self):
      times = 1000
      no_dir_domain = 'www.nodir.com'
      self.domain_directory.append(no_dir_domain)
      self.assertEqual(4, len(self.domain_directory))
      url = 'http://127.1.0.1:{0}'.format(self.configs['records.config']['CONFIG']['proxy.config.http.server_ports'])
      for i in xrange(times):
          domain = random.choice(self.domain_directory)
          headers = {'Host': domain}
          r = requests.get(url, headers=headers)
          domain_in_response = no_dir_domain
          for domain_item in self.domain_directory:
              if domain_item in r.text:
                   domain_in_response = domain_item
                   break
          self.assertEqual(domain, domain_in_response)
