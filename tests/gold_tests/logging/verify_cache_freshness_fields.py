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


def load_log_entries(log_path: pathlib.Path) -> dict[str, tuple[int, int, str]]:
    entries: dict[str, tuple[int, int, str]] = {}

    for line in log_path.read_text(encoding='utf-8').splitlines():
        uuid, freshness_limit, current_age, cache_result = line.split()
        entries[uuid] = (int(freshness_limit), int(current_age), cache_result)

    return entries


def main() -> int:
    entries = load_log_entries(pathlib.Path(sys.argv[1]))
    expected_uuids = {'cache-write', 'cache-hit', 'uncacheable'}

    if entries.keys() != expected_uuids:
        print(f'Expected entries for {sorted(expected_uuids)}, got {sorted(entries)}', file=sys.stderr)
        return 1

    write_freshness, write_age, write_result = entries['cache-write']
    if write_freshness != 60 or write_age != -1 or 'MISS' not in write_result:
        print(f'Unexpected cache-write entry: {entries["cache-write"]}', file=sys.stderr)
        return 1

    hit_freshness, hit_age, hit_result = entries['cache-hit']
    if hit_freshness != 60 or hit_age < 7 or 'HIT' not in hit_result or 'MISS' in hit_result:
        print(f'Unexpected cache-hit entry: {entries["cache-hit"]}', file=sys.stderr)
        return 1

    if entries['uncacheable'][:2] != (-1, -1):
        print(f'Unexpected uncacheable entry: {entries["uncacheable"]}', file=sys.stderr)
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
