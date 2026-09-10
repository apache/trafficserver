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

Test.Summary = '''
traffic_ctl JSON output must be parseable JSON, including when a node is null.

yaml-cpp emits null as `~`, which is valid YAML but rejected by every JSON
parser. Emitters that produce JSON must set YAML::LowerNull, and container
nodes that may stay empty must be constructed as sequences so they emit `[]`
rather than null.

The trigger for both regressions is the *empty* case, so this test
deliberately runs against a freshly started server with an empty HostDB and no
plugins loaded. A test that populates either one first would pass against the
bug.
'''

Test.ContinueOnFail = True

records_yaml = '''
  exec_thread:
    autoconfig:
      enabled: 0
    limit: 4
    '''

traffic_ctl = Make_traffic_ctl(Test, records_yaml)

######
# hostdb status -- `partitions` is empty on a fresh server.
#
# Flagless output goes through BasePrinter::write_output_json (the client
# printer). Before the fix this emitted `"partitions": ~`.
traffic_ctl.hostdb().status().validate_is_valid_json()

# ... and it must be an empty array, not null. hostdb_status_schema.json
# declares partitions as "type": "array".
traffic_ctl.hostdb().status().validate_json_contains(partitions=[])

# -f json goes through the full envelope. Same emitter, different entry point.
traffic_ctl.hostdb().status().as_json().validate_is_valid_json()

# The server-side encoder (yamlcpp_json_encoder) is a third, independent
# emitter. rpc invoke exercises it directly.
#
# The params are required: get_hostdb_status without them fails with "invalid
# node; this may result from using a map iterator as a sequence iterator", and
# an error envelope is valid JSON no matter what the emitter does -- the
# assertion would pass against the bug.
traffic_ctl.rpc().invoke(handler="get_hostdb_status", params='"hostname: \\"\\""').validate_is_valid_json()

######
# plugin list -- `plugins` is empty when plugin.config loads nothing.
#
# plugin list ignores the format flag today and prints a human table, so only
# the RPC path is assertable. Once plugin list honours -f json, add:
#   traffic_ctl.plugin().list().as_json().validate_is_valid_json()
#
# Assert the shape rather than mere parseability: `plugins` has to be `[]`,
# matching what the hostdb case above asserts for `partitions`. The field sits
# at result.data.plugins, which validate_json_contains cannot reach, so this
# compares the whole result, as the connection tracker cases in
# traffic_ctl_server_output.test.py do. Before the fix the field emitted `~`,
# which fails this comparison as surely as it fails a JSON parser. Nothing
# forces plugin.config to be empty here, and nothing needs to: should a
# default ever load a plugin, this assertion fails rather than going quiet.
traffic_ctl.rpc().invoke(
    handler="admin_plugin_get_list").validate_result_with_text('{"data": {"source": "plugin.config", "plugins": []}}')

######
# Commands that were already valid JSON -- guard against the shared emitter
# change regressing them.
traffic_ctl.server().status().validate_is_valid_json()
traffic_ctl.rpc().invoke(handler="show_registered_handlers").validate_is_valid_json()
