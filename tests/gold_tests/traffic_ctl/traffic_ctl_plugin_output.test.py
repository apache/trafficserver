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

import sys

# To include util classes
sys.path.insert(0, f'{Test.TestDirectory}')

from traffic_ctl_test_utils import Make_traffic_ctl

Test.Summary = 'Test traffic_ctl plugin list output in text and json format.'

Test.ContinueOnFail = True

records_yaml = '''
  exec_thread:
    autoconfig:
      enabled: 0
    limit: 4
    '''

# xdebug logs an error and does nothing when no feature is enabled. The
# reported path is argv[0] only, so the argument does not reach the table.
plugin_config = ['xdebug.so --enable=x-cache', 'stats_over_http.so']

traffic_ctl = Make_traffic_ctl(Test, records_yaml, plugin_config=plugin_config)

# The payload is asserted through json, not through the table. Column widths
# are a presentation detail, so pinning them down only makes the test brittle
# against cosmetic changes. This checks the whole structure, so a renamed,
# added or dropped key fails too.
traffic_ctl.plugin().list().as_json().validate_json_data_matches(
    {
        'source': 'plugin.config',
        'plugins':
            [
                {
                    'path': 'xdebug.so',
                    'enabled': 'true',
                    'status': 'loaded',
                    'index': '1'
                },
                {
                    'path': 'stats_over_http.so',
                    'enabled': 'true',
                    'status': 'loaded',
                    'index': '2'
                },
            ],
    })

# Json mode emits the whole jsonrpc envelope, not just the payload. Asserting
# a top-level field is what separates the two.
traffic_ctl.plugin().list().as_json().validate_json_contains(jsonrpc='2.0')

# Text mode only needs to still render a table. This is a smoke check that the
# formatter survived being moved into PluginListPrinter; it deliberately does
# not assert the column layout.
traffic_ctl.plugin().list().validate_contains_all('source: plugin.config', 'xdebug.so', 'stats_over_http.so', 'loaded')
