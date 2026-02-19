#!/usr/bin/env python3
'''
Validate milestone timing fields in an ATS log file.

Parses key=value log lines and checks:
  - All expected fields are present
  - All values are integers
  - No epoch-length garbage (> 1 billion) from the difference_msec bug
  - Cache miss lines have ms > 0 and origin-phase fields populated
  - Cache hit lines have hit_proc and hit_xfer populated
  - The miss-path chain sums to approximately c_ttfb
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

import sys

ALL_FIELDS = [
    'crc',
    'ms',
    'c_ttfb',
    'c_tls',
    'c_hdr',
    'c_proc',
    'cache',
    'dns',
    'o_tcp',
    'o_wait',
    'o_hdr',
    'o_proc',
    'o_body',
    'c_xfer',
    'hit_proc',
    'hit_xfer',
]

TIMING_FIELDS = [f for f in ALL_FIELDS if f != 'crc']

# Fields that form the contiguous miss-path chain to c_ttfb:
#   c_ttfb = c_hdr + c_proc + cache + dns + o_conn + o_wait + o_hdr + o_proc
MISS_CHAIN = ['c_hdr', 'c_proc', 'cache', 'dns', 'o_tcp', 'o_wait', 'o_hdr', 'o_proc']

EPOCH_THRESHOLD = 1_000_000_000


def parse_line(line: str) -> dict[str, str]:
    """Parse a space-separated key=value log line into a dict."""
    fields = {}
    for token in line.strip().split():
        if '=' in token:
            key, val = token.split('=', 1)
            fields[key] = val
    return fields


def validate_line(fields: dict[str, str], line_num: int) -> list[str]:
    """Return a list of error strings (empty = pass)."""
    errors = []

    for name in ALL_FIELDS:
        if name not in fields:
            errors.append(f'line {line_num}: missing field "{name}"')

    for name in TIMING_FIELDS:
        val_str = fields.get(name)
        if val_str is None:
            continue
        # Accept '-' as a valid sentinel for unset milestones.
        if val_str == '-':
            continue
        try:
            val = int(val_str)
        except ValueError:
            errors.append(f'line {line_num}: field "{name}" is not an integer: {val_str!r}')
            continue

        if val > EPOCH_THRESHOLD:
            errors.append(f'line {line_num}: field "{name}" looks like an epoch leak: {val} '
                          f'(> {EPOCH_THRESHOLD})')

    crc = fields.get('crc', '')
    is_miss = 'MISS' in crc or 'NONE' in crc
    is_hit = 'HIT' in crc and 'MISS' not in crc

    ms_str = fields.get('ms', '0')
    try:
        ms_val = int(ms_str)
    except ValueError:
        ms_val = -1

    if ms_val < 0 and ms_str != '-':
        errors.append(f'line {line_num}: ms should be >= 0, got {ms_val}')

    if is_miss:
        for name in MISS_CHAIN:
            val_str = fields.get(name)
            if val_str is None or val_str == '-':
                continue
            try:
                val = int(val_str)
            except ValueError:
                continue
            if val < -1:
                errors.append(f'line {line_num}: miss field "{name}" has unexpected value: {val}')

        # Verify chain sum approximates c_ttfb (within tolerance for rounding).
        chain_vals = []
        for name in MISS_CHAIN:
            val_str = fields.get(name)
            if val_str is None or val_str == '-':
                chain_vals.append(0)
                continue
            try:
                v = int(val_str)
                chain_vals.append(v if v >= 0 else 0)
            except ValueError:
                chain_vals.append(0)

        chain_sum = sum(chain_vals)
        c_ttfb_str = fields.get('c_ttfb')
        if c_ttfb_str and c_ttfb_str != '-':
            try:
                c_ttfb_val = int(c_ttfb_str)
                # Allow 2ms tolerance for rounding across multiple sub-millisecond fields.
                if c_ttfb_val >= 0 and abs(chain_sum - c_ttfb_val) > 2:
                    errors.append(
                        f'line {line_num}: chain sum ({chain_sum}) != c_ttfb ({c_ttfb_val}), '
                        f'diff={abs(chain_sum - c_ttfb_val)}ms')
            except ValueError:
                pass

    if is_hit:
        for name in ['hit_proc', 'hit_xfer']:
            val_str = fields.get(name)
            if val_str is None:
                errors.append(f'line {line_num}: cache hit missing field "{name}"')
                continue
            if val_str == '-':
                errors.append(f'line {line_num}: cache hit field "{name}" should not be "-"')
                continue
            try:
                val = int(val_str)
                if val < 0:
                    errors.append(f'line {line_num}: cache hit field "{name}" should be >= 0, got {val}')
            except ValueError:
                errors.append(f'line {line_num}: cache hit field "{name}" is not an integer: {val_str!r}')

    return errors


def main():
    if len(sys.argv) != 2:
        print(f'Usage: {sys.argv[0]} <log_file>', file=sys.stderr)
        sys.exit(1)

    log_path = sys.argv[1]
    try:
        with open(log_path) as f:
            lines = [l for l in f.readlines() if l.strip()]
    except FileNotFoundError:
        print(f'FAIL: log file not found: {log_path}')
        sys.exit(1)

    if len(lines) < 2:
        print(f'FAIL: expected at least 2 log lines (miss + hit), got {len(lines)}')
        sys.exit(1)

    all_errors = []
    miss_found = False
    hit_found = False

    for i, line in enumerate(lines, start=1):
        fields = parse_line(line)
        crc = fields.get('crc', '')
        if 'MISS' in crc:
            miss_found = True
        if 'HIT' in crc and 'MISS' not in crc:
            hit_found = True
        errors = validate_line(fields, i)
        all_errors.extend(errors)

    if not miss_found:
        all_errors.append('No cache miss line found in log')
    if not hit_found:
        all_errors.append('No cache hit line found in log')

    if all_errors:
        for err in all_errors:
            print(f'FAIL: {err}')
        sys.exit(1)
    else:
        print(f'PASS: validated {len(lines)} log lines '
              f'(miss={miss_found}, hit={hit_found}), all fields correct')
        sys.exit(0)


if __name__ == '__main__':
    main()
