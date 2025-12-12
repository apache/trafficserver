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

# Test 0: YAML output
traffic_ctl.config().get("proxy.config.diags.debug.tags").as_records().validate_with_goldfile("t1_yaml.gold")
# Test 1: Default output
traffic_ctl.config().get("proxy.config.diags.debug.enabled").validate_with_text("proxy.config.diags.debug.enabled: 1")
# Test 2: Default output with default.
traffic_ctl.config().get("proxy.config.diags.debug.tags").with_default() \
    .validate_with_text("proxy.config.diags.debug.tags: rpc # default http|dns")

# Test 3: Now same output test but with defaults, traffic_ctl supports adding default value
# when using --records.
traffic_ctl.config().get("proxy.config.diags.debug.tags").as_records().with_default().validate_with_goldfile("t2_yaml.gold")
# Test 4:
traffic_ctl.config().get(
    "proxy.config.diags.debug.tags proxy.config.diags.debug.enabled proxy.config.diags.debug.throttling_interval_msec").as_records(
    ).with_default().validate_with_goldfile("t3_yaml.gold")

##### CONFIG MATCH
# Test 5:
traffic_ctl.config().match("threads").with_default().validate_with_goldfile("match.gold")

# Test 6: The idea is to check the traffic_ctl yaml emitter when a value starts with the
# same prefix of a node like:
# diags:
#    logfile:
#    logfile_perm: rw-r--r--
#
# traffic_ctl have a special logic to deal with cases like this, so better test it.
traffic_ctl.config().match("diags.logfile").as_records().validate_with_goldfile("t4_yaml.gold")

##### CONFIG DIFF
# Test 7:
traffic_ctl.config().diff().validate_with_goldfile("diff.gold")
# Test 8:
traffic_ctl.config().diff().as_records().validate_with_goldfile("diff_yaml.gold")

##### CONFIG DESCRIBE
# Test 9: don't really care about values, but just output and that the command actually went through
traffic_ctl.config().describe("proxy.config.http.server_ports").validate_with_goldfile("describe.gold")

##### CONFIG RESET
# Test 10: Reset a single modified record (proxy.config.diags.debug.tags is set to "rpc" in records_yaml,
# default is "http|dns", so it should be reset)
traffic_ctl.config().reset("proxy.config.diags.debug.tags").validate_with_text(
    "Set proxy.config.diags.debug.tags, please wait 10 seconds for traffic server to sync "
    "configuration, restart is not required")
# Test 11: Validate the record was reset to its default value
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: http|dns")

# Test 12: Reset records matching a partial path (proxy.config.diags)
# First set the record back to non-default for this test
traffic_ctl.config().set("proxy.config.diags.debug.tags", "rpc").exec()
# Test 13: Resetting proxy.config.diags should reset all matching modified records under that path
traffic_ctl.config().reset("proxy.config.diags").validate_contains_all(
    "Set proxy.config.diags.debug.tags", "Set proxy.config.diags.debug.enabled")
# Test 14: Validate the record was reset to its default value
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: http|dns")

# Test 15: Reset all records using "records" keyword
# First set the record back to non-default for this test
traffic_ctl.config().set("proxy.config.diags.debug.tags", "rpc").exec()
# Test 16: This will reset all modified records (including proxy.config.diags.debug.tags)
# Some may require restart, which is ok, we can use diff anyways as the records that needs
# restart will just change the value but won't have any effect.
traffic_ctl.config().reset("records").exec()
# Validate the diff
# Test 17: Validate the diff
traffic_ctl.config().diff().validate_with_text("")
# Test 18: Validate the record was reset to its default value
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: http|dns")

# # Test resetting when no records need resetting (all already at default)
# # Create a new instance with default values only
# traffic_ctl_default = Make_traffic_ctl(Test, None)
# traffic_ctl_default.config().reset("proxy.config.diags.debug.enabled").validate_with_text(
#     "No records to reset (all matching records are already at default values)")

##### CONFIG RESET with YAML-style paths (records.* format)
# Test 19: Set a record to non-default first
traffic_ctl.config().set("proxy.config.diags.debug.tags", "yaml_test").exec()
# Test 20: Reset using YAML-style path (records.diags.debug.tags instead of proxy.config.diags.debug.tags)
traffic_ctl.config().reset("records.diags.debug.tags").validate_with_text(
    "Set proxy.config.diags.debug.tags, please wait 10 seconds for traffic server to sync "
    "configuration, restart is not required")
# Test 21: Validate the record was reset to its default value
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: http|dns")

# Test 22: Reset using YAML-style partial path (records.diags)
traffic_ctl.config().set("proxy.config.diags.debug.tags", "yaml_partial_test").exec()
traffic_ctl.config().set("proxy.config.diags.debug.enabled", "1").exec()
# Test 23: Reset using records.diags (YAML format)
traffic_ctl.config().reset("records.diags").validate_contains_all(
    "Set proxy.config.diags.debug.tags", "Set proxy.config.diags.debug.enabled")
# Test 24: Validate record was reset
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: http|dns")

# Test 25: Make sure that the command returns an exit code of 2
traffic_ctl.config().get("invalid.should.set.the.exit.code.to.2").validate_with_exit_code(2)
