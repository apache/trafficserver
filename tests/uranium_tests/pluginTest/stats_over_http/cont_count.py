'''
Read the INKContAllocator in-use count from the memory dumps that Traffic Server writes to traffic.out.

Run Traffic Server with --disable_pfreelist and proxy.config.dump_mem_info_frequency: 1.  With --disable_pfreelist, a
destroyed continuation skips the per-thread freelist and the allocator returns it to malloc, so the in-use count is the
number of live continuations.
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

import argparse
import os
import re
import sys
import time

# A row of the ink_freelists_dump() table.  The fourth column is the in-use count.
CONT_ROW = re.compile(r'^(?:[^|\n]*\|){3}\s*(\d+)\s*\|[^\n]*memory/INKContAllocator$', re.MULTILINE)


def counts_after(path: str, offset: int) -> list[int]:
    '''Return the in-use counts from the dump rows that begin after offset.'''
    with open(path, 'rb') as f:
        f.seek(offset)
        text = f.read().decode(errors='replace')
    if offset > 0:
        text = text.partition('\n')[2]
    return [int(match.group(1)) for match in CONT_ROW.finditer(text)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('traffic_out', help='traffic.out of a Traffic Server that dumps its memory use every second')
    parser.add_argument('baseline', help='the file that holds the baseline count')
    parser.add_argument(
        '--save', action='store_true', help='save the count from the next dump as the baseline instead of checking against it')
    parser.add_argument('--timeout', type=float, default=30, help='seconds to wait for a matching dump')
    args = parser.parse_args()

    offset = os.path.getsize(args.traffic_out)
    baseline = None
    if not args.save:
        with open(args.baseline) as f:
            baseline = int(f.read())

    counts = []
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        counts = counts_after(args.traffic_out, offset)
        if args.save and counts:
            with open(args.baseline, 'w') as f:
                f.write(str(counts[0]))
            print(f'{counts[0]} continuations in use')
            return 0
        if baseline is not None and any(count <= baseline for count in counts):
            print(f'{min(counts)} continuations in use, no more than the baseline of {baseline}')
            return 0
        time.sleep(0.25)

    print(f'Continuations in use after {args.timeout} s: {counts or "no memory dump"}, baseline {baseline}')
    return 1


if __name__ == '__main__':
    sys.exit(main())
