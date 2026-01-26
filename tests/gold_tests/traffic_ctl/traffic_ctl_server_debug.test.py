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
Test traffic_ctl server debug enable/disable commands.
'''

Test.ContinueOnFail = True

records_yaml = '''
diags:
  debug:
    enabled: 0
    tags: xyz
'''

traffic_ctl = Make_traffic_ctl(Test, records_yaml)

######
# Test 1: Enable debug with tags
traffic_ctl.server().debug().enable(tags="http").exec()
# Test 2: Verify debug is enabled and tags are set
traffic_ctl.config().get("proxy.config.diags.debug.enabled").validate_with_text("proxy.config.diags.debug.enabled: 1")
# Test 3: Verify tags are set
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: http")

# Test 4: Disable debug
traffic_ctl.server().debug().disable().exec()
# Test 5: Verify debug is disabled
traffic_ctl.config().get("proxy.config.diags.debug.enabled").validate_with_text("proxy.config.diags.debug.enabled: 0")

# Test 6: Enable debug with new tags (replace mode)
traffic_ctl.server().debug().enable(tags="cache").exec()
# Test 7: Verify tags are replaced
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: cache")

# Test 8: Enable debug with append mode - should combine with existing tags
traffic_ctl.server().debug().enable(tags="http", append=True).exec()
# Test 9: Verify tags are appended
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: cache|http")

# Test 10: Append another tag
traffic_ctl.server().debug().enable(tags="dns", append=True).exec()
# Test 11: Verify all tags are present
traffic_ctl.config().get("proxy.config.diags.debug.tags").validate_with_text("proxy.config.diags.debug.tags: cache|http|dns")

# Test 12: Disable and verify
traffic_ctl.server().debug().disable().exec()
# Test 13: Verify debug is disabled
traffic_ctl.config().get("proxy.config.diags.debug.enabled").validate_with_text("proxy.config.diags.debug.enabled: 0")

# Test 14: Verify --append requires --tags (should fail with error)
# This tests the ArgParser requires() functionality
tr = Test.AddTestRun("test --append without --tags")
tr.Processes.Default.Env = traffic_ctl._ts.Env
tr.Processes.Default.Command = "traffic_ctl server debug enable --append"
tr.Processes.Default.ReturnCode = 64  # EX_USAGE - command line usage error
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "Option \'--append\' requires \'--tags\' to be specified", "Should show error that --append requires --tags")
tr.StillRunningAfter = traffic_ctl._ts
