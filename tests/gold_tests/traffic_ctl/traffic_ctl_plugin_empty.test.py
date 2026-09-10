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

Test.Summary = 'Test traffic_ctl plugin list with no plugins loaded.'

Test.ContinueOnFail = True

records_yaml = '''
  exec_thread:
    autoconfig:
      enabled: 0
    limit: 4
    '''

# No plugin_config, so the default empty plugin.config is used. This is the
# case a populated-config test never reaches, and the one that produced the
# unparseable `plugins: ~` before the json emitter was fixed.
traffic_ctl = Make_traffic_ctl(Test, records_yaml)

# An empty list, not null. A client iterating this field should not have to
# special case a server with nothing loaded. Asserting the whole structure is
# what makes [] distinguishable from null here.
traffic_ctl.plugin().list().as_json().validate_json_data_matches({'source': 'plugin.config', 'plugins': []})

traffic_ctl.plugin().list().validate_contains_all('source: plugin.config')
