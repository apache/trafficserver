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
Test that a catastrophic regex given to traffic_ctl config match cannot stall the RPC server.
'''

Test.ContinueOnFail = True

# Record names are letters and dots, so "([a-z.]+)+" can partition any of them a great many ways,
# and the trailing class holds characters no name contains, so the match can never complete. No
# crafted records are needed: against the 1328 names a stock server carries this costs roughly 20s
# of CPU unbounded, against about 120ms once the match limit applies.
#
# Two details are load bearing. The tail must be a character class rather than a literal, because
# for an absent literal PCRE2's required-code-unit check rejects each name up front and no
# backtracking happens, leaving the test to pass with or without the fix. And it must be
# unsatisfiable rather than merely unlikely; an earlier "[0-9]\z" tail matched
# proxy.config.ssl.TLSv1, as lookups are case insensitive. The "(a+)+$" commonly cited is
# useless here, no real name holds two consecutive 'a' characters to backtrack over.
BACKTRACKING_PATTERN = '^([a-z.]+)+[~=]'

records_yaml = '''
    diags:
      debug:
        enabled: 0
    '''

traffic_ctl = Make_traffic_ctl(Test, records_yaml)

# Every name is abandoned at the limit, so nothing matches; that the command returns promptly is
# the point, not the empty result.
traffic_ctl.config().match(f"'{BACKTRACKING_PATTERN}'").validate_with_text("")

# An ordinary lookup afterwards shows the RPC server was left responsive rather than wedged.
traffic_ctl.config().match("proxy.config.diags.debug.enabled") \
    .validate_with_text("proxy.config.diags.debug.enabled: 0")

# Truncating silently would hand the caller a short answer with no sign of it. Unlike a timing
# check, this assertion does not depend on machine speed.
traffic_ctl.ts.Disk.diags_log.Content += Testers.IncludesExpression(
    "results are incomplete", "the truncated lookup should be reported, not silently returned as a short answer")
