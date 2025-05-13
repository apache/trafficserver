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
import os

# To include util classes
sys.path.insert(0, f'{Test.TestDirectory}')

from traffic_ctl_test_utils import Make_traffic_ctl
# import ruamel.yaml Uncomment only when GoldFilePathFor is used.

Test.Summary = '''
Test traffic_ctl config output responses.
'''

Test.ContinueOnFail = True

records_yaml = '''
    udp:
      threads: 1
    diags:
      debug:
        enabled: 1
        tags: rpc
        throttling_interval_msec: 0
    '''

traffic_ctl = Make_traffic_ctl(Test, records_yaml)

##### CONFIG GET

# YAML output
traffic_ctl.config().get("proxy.config.diags.debug.tags").as_records().validate_with_goldfile("t1_yaml.gold")
# Default output
traffic_ctl.config().get("proxy.config.diags.debug.enabled").validate_with_text("proxy.config.diags.debug.enabled: 1")
# Default output with default.
traffic_ctl.config().get("proxy.config.diags.debug.tags").with_default() \
    .validate_with_text("proxy.config.diags.debug.tags: rpc # default http|dns")

# Now same output test but with defaults, traffic_ctl supports adding default value
# when using --records.
traffic_ctl.config().get("proxy.config.diags.debug.tags").as_records().with_default().validate_with_goldfile("t2_yaml.gold")
traffic_ctl.config().get(
    "proxy.config.diags.debug.tags proxy.config.diags.debug.enabled proxy.config.diags.debug.throttling_interval_msec").as_records(
    ).with_default().validate_with_goldfile("t3_yaml.gold")

##### CONFIG MATCH
traffic_ctl.config().match("threads").with_default().validate_with_goldfile("match.gold")

# The idea is to check the traffic_ctl yaml emitter when a value starts with the
# same prefix of a node like:
# diags:
#    logfile:
#    logfile_perm: rw-r--r--
#
# traffic_ctl have a special logic to deal with cases like this, so better test it.
traffic_ctl.config().match("diags.logfile").as_records().validate_with_goldfile("t4_yaml.gold")

##### CONFIG DIFF
traffic_ctl.config().diff().validate_with_goldfile("diff.gold")
traffic_ctl.config().diff().as_records().validate_with_goldfile("diff_yaml.gold")

##### CONFIG DESCRIBE
# don't really care about values, but just output and that the command actually went through
traffic_ctl.config().describe("proxy.config.http.server_ports").validate_with_goldfile("describe.gold")
