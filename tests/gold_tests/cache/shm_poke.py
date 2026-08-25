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
"""Flip raw bytes in a cache shm segment file, for shm trust-gate autests.

On Linux the POSIX shm segments are plain files under /dev/shm, so a segment
left behind by a clean shutdown can be tampered with between runs to drive the
control-segment trust gates (schema/ABI mismatch, an unterminated shm_name,
etc.). This is Linux-only: macOS POSIX shm segments are not path-addressable.

Usage:
    shm_poke.py <path> <byte_offset> <hex_bytes>

Example (set schema_version @8 to 9, little-endian uint32):
    shm_poke.py /dev/shm/cshmx-12345-control 8 09000000
"""

import sys


def main() -> int:
    if len(sys.argv) != 4:
        sys.stderr.write(f'usage: {sys.argv[0]} <path> <byte_offset> <hex_bytes>\n')
        return 2
    path = sys.argv[1]
    offset = int(sys.argv[2], 0)
    data = bytes.fromhex(sys.argv[3])
    with open(path, 'r+b') as f:
        f.seek(offset)
        f.write(data)
    return 0


if __name__ == '__main__':
    sys.exit(main())
