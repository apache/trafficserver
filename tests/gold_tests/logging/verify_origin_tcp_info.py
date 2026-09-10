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
"""Validate origin TCP_INFO access-log fields after replay traffic completes."""

import argparse
from pathlib import Path
import time


def verify(log_path: Path, mode: str) -> None:
    expected_keys = {
        'disabled': {'miss'},
        'enabled': {'miss', 'hit', 'guard', 'oversized', 'malformed'},
        'retry': {'retry'},
    }[mode]
    # Wait for the asynchronous log writer, rather than sleeping a fixed time.
    deadline = time.monotonic() + 15
    while True:
        contents = log_path.read_text() if log_path.exists() else ''
        # A final record without its newline may still be partially written.
        lines = contents.split('\n')[:-1]
        if len(lines) >= len(expected_keys):
            break
        if time.monotonic() >= deadline:
            raise AssertionError(f'Timed out waiting for access-log records for {expected_keys}: {contents!r}')
        time.sleep(0.1)

    if len(lines) != len(expected_keys):
        raise AssertionError(f'Expected {len(expected_keys)} access-log records: {lines}')

    rows = {}
    for line in lines:
        fields = line.split()
        if len(fields) != 6:
            raise AssertionError(f'Expected a transaction ID, cache result, and four TCP_INFO fields: {line}')
        key, cache_result, *values = fields
        if key in rows:
            raise AssertionError(f'Duplicate transaction ID: {key}')
        rows[key] = (cache_result, [int(value) for value in values])

    if set(rows) != expected_keys:
        raise AssertionError(f'Unexpected transaction IDs: {rows}')

    for key in expected_keys & {'miss', 'guard'}:
        if rows[key][0] != 'TCP_MISS':
            raise AssertionError(f'{key} must reach the origin: {rows[key]}')
    if 'hit' in rows and rows['hit'][0] not in ('TCP_HIT', 'TCP_MEM_HIT'):
        raise AssertionError(f'Expected a cache hit: {rows["hit"]}')

    for key, (_, values) in rows.items():
        if mode == 'enabled' and key == 'miss':
            rtt, rttvar, retrans, cwnd = values
            if not (rtt > 0 and rttvar >= 0 and retrans >= 0 and cwnd > 0):
                raise AssertionError(f'Expected a valid origin TCP_INFO sample: {values}')
        elif values != [-1, -1, -1, -1]:
            raise AssertionError(f'{key} must have no TCP_INFO sample with sampling {mode}: {values}')

    print(f'PASS: origin TCP_INFO sampling {mode}')
    print('\n'.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log_path', type=Path)
    parser.add_argument('mode', choices=('disabled', 'enabled', 'retry'))
    args = parser.parse_args()
    verify(args.log_path, args.mode)
