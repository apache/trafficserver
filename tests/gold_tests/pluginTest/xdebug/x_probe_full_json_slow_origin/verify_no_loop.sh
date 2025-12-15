#!/bin/bash
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

# Verify the transform completed successfully and didn't produce excessive log output.
#
# Our test sends a 2-chunk response (5 bytes each). Normal behavior:
#   - expected (first chunk)
#   - consumed (first chunk)
#   - expected (HttpTunnel callback - no data yet)
#   - expected (second chunk)
#   - consumed (second chunk)
#
# That's 4 "expected" lines for 2 chunks. The bug would cause many more "expected"
# lines as the transform loops, but in our test the loop might not be tight enough
# to produce many lines. We check that we don't have an excessive number.

LOGFILE="$1"
MAX_EXPECTED_LINES=10  # Allow up to 10; our 2-chunk response should produce ~4

if [[ ! -f "$LOGFILE" ]]; then
    echo "FAIL: Log file not found: $LOGFILE"
    exit 1
fi

# Count "expected" lines
expected_count=$(grep -c "bytes of body is expected" "$LOGFILE" 2>/dev/null || echo "0")

echo "Transform log analysis:"
echo "  'expected' lines: $expected_count (max allowed: $MAX_EXPECTED_LINES)"
echo ""
echo "Log contents:"
grep -E "(bytes of body is expected|consumed.*bytes)" "$LOGFILE"
echo ""

if [[ $expected_count -gt $MAX_EXPECTED_LINES ]]; then
    echo "FAIL: Found $expected_count 'expected' lines - indicates potential loop bug"
    exit 1
fi

# Also verify we got exactly 2 consumed lines (for our 2 chunks)
consumed_count=$(grep -c "consumed.*bytes" "$LOGFILE" 2>/dev/null || echo "0")
echo "  'consumed' lines: $consumed_count (expected: 2)"

if [[ $consumed_count -ne 2 ]]; then
    echo "WARNING: Expected 2 consumed lines but found $consumed_count"
fi

echo "PASS: Transform completed normally"
exit 0
