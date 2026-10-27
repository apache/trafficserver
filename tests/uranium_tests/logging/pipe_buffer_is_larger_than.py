#!/usr/bin/env python
'''
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
import fcntl
from pathlib import Path
import sys
import time

F_SETPIPE_SZ = 1031  # Linux 2.6.35+
F_GETPIPE_SZ = 1032  # Linux 2.6.35+


def parse_args() -> argparse.Namespace:
    """Read the FIFO capacity and diagnostic log to verify."""
    parser = parser = argparse.ArgumentParser(description='Verify that a FIFO has a buffer of at least a certain size')

    parser.add_argument('pipe_name', help='The pipe name upon which to verify the size is large enough.')

    parser.add_argument('minimum_buffer_size', help='The minimu buffer size for the pipe to expect.')
    parser.add_argument('diags_log', type=Path, help='Diagnostics containing any kernel permission error.')

    return parser.parse_args()


def test_fifo(fifo: str, minimum_buffer_size: str, diags_log: Path) -> int:
    """Require the requested capacity or an explicit kernel permission denial.

    :param fifo: Named pipe to inspect.
    :param minimum_buffer_size: Required capacity in bytes.
    :param diags_log: ATS diagnostic log, polled for up to five seconds.
    """
    try:
        fifo_fd = open(fifo, "rb+", buffering=0)
        buffer_size = fcntl.fcntl(fifo_fd, F_GETPIPE_SZ)

        if buffer_size >= int(minimum_buffer_size):
            print("Success. Size is: {} which is larger than: {}".format(buffer_size, minimum_buffer_size))
            return 0
        for _ in range(50):
            if diags_log.exists():
                diagnostics = diags_log.read_text(errors="replace")
                if "Set pipe size failed" in diagnostics and "Operation not permitted" in diagnostics:
                    print("Success. The kernel denied increasing the pipe buffer for the unprivileged ATS user.")
                    return 0
            time.sleep(0.1)
        print(f"Fail. Size is: {buffer_size} which is smaller than: {minimum_buffer_size}")
        return 1
    except Exception as e:
        print("Unable to open fifo, error: {}".format(str(e)))
        return 2


def main() -> int:
    """Run the FIFO capacity check."""
    args = parse_args()
    return test_fifo(args.pipe_name, args.minimum_buffer_size, args.diags_log)


if __name__ == '__main__':
    sys.exit(main())
