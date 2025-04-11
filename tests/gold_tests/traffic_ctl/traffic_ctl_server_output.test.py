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

Test.Summary = '''
Test traffic_ctl different commands.
'''

Test.ContinueOnFail = True

Test.Summary = 'Basic test for traffic_ctl server command features.'

traffic_ctl = Make_traffic_ctl(Test)
######
# traffic_ctl server status
traffic_ctl.server().status().validate_with_text(
    '{"initialized_done": "true", "is_ssl_handshaking_stopped": "false", "is_draining": "false", "is_event_system_shut_down": "false"}'
)
# Drain ats so we can check the output.
traffic_ctl.server().drain().exec()

# After the drain, we should see that the status should reflect this change.
traffic_ctl.server().status().validate_with_text(
    '{"initialized_done": "true", "is_ssl_handshaking_stopped": "false", "is_draining": "true", "is_event_system_shut_down": "false"}'
)
