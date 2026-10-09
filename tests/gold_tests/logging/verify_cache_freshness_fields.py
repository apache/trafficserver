'''
Validate the cache freshness fields produced by cache-freshness-fields.test.py.
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

import pathlib
import sys


def load_log_entries(log_path: pathlib.Path) -> dict[str, tuple[int, int, str, str]]:
    entries: dict[str, tuple[int, int, str, str]] = {}

    for line_number, line in enumerate(log_path.read_text(encoding='utf-8').splitlines(), start=1):
        fields = line.split()
        if len(fields) != 5:
            raise ValueError(f'{log_path}:{line_number}: expected 5 fields, got {len(fields)}: {line!r}')

        uuid, freshness_limit, current_age, cache_result, cache_write_result = fields
        if uuid in entries:
            raise ValueError(f'{log_path}:{line_number}: duplicate UUID {uuid!r}')

        entries[uuid] = (int(freshness_limit), int(current_age), cache_result, cache_write_result)

    return entries


def main() -> int:
    try:
        entries = load_log_entries(pathlib.Path(sys.argv[1]))
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1

    expected_uuids = {
        'cache-write',
        'cache-hit',
        'negative-revalidation-write',
        'negative-revalidation',
        'failed-cache-write',
        'uncacheable',
    }

    if entries.keys() != expected_uuids:
        print(f'Expected entries for {sorted(expected_uuids)}, got {sorted(entries)}', file=sys.stderr)
        return 1

    write_freshness, write_age, write_result, _ = entries['cache-write']
    if write_freshness != 60 or write_age != -1 or 'MISS' not in write_result:
        print(f'Unexpected cache-write entry: {entries["cache-write"]}', file=sys.stderr)
        return 1

    hit_freshness, hit_age, hit_result, _ = entries['cache-hit']
    if hit_freshness != 60 or hit_age < 7 or 'HIT' not in hit_result or 'MISS' in hit_result:
        print(f'Unexpected cache-hit entry: {entries["cache-hit"]}', file=sys.stderr)
        return 1

    negative_write_freshness, negative_write_age, negative_write_result, _ = entries['negative-revalidation-write']
    if negative_write_freshness != 1 or negative_write_age != -1 or 'MISS' not in negative_write_result:
        print(f'Unexpected negative-revalidation-write entry: {entries["negative-revalidation-write"]}', file=sys.stderr)
        return 1

    negative_freshness, negative_age, negative_result, _ = entries['negative-revalidation']
    if negative_freshness != 1 or negative_age < 1 or 'REFRESH_FAIL_HIT' not in negative_result:
        print(f'Unexpected negative-revalidation entry: {entries["negative-revalidation"]}', file=sys.stderr)
        return 1

    if entries['failed-cache-write'][:2] != (-1, -1) or entries['failed-cache-write'][3] != 'ERR':
        print(f'Unexpected failed-cache-write entry: {entries["failed-cache-write"]}', file=sys.stderr)
        return 1

    if entries['uncacheable'][:2] != (-1, -1):
        print(f'Unexpected uncacheable entry: {entries["uncacheable"]}', file=sys.stderr)
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
