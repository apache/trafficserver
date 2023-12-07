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
import sys

F_SETPIPE_SZ = 1031  # Linux 2.6.35+
F_GETPIPE_SZ = 1032  # Linux 2.6.35+


def parse_args():
    parser = parser = argparse.ArgumentParser(description='Verify that a FIFO has a buffer of at least a certain size')

    parser.add_argument('pipe_name', help='The pipe name upon which to verify the size is large enough.')

    parser.add_argument('minimum_buffer_size', help='The minimu buffer size for the pipe to expect.')

    return parser.parse_args()


def test_fifo(fifo, minimum_buffer_size):
    try:
        fifo_fd = open(fifo, "rb+", buffering=0)
        buffer_size = fcntl.fcntl(fifo_fd, F_GETPIPE_SZ)

        if buffer_size >= int(minimum_buffer_size):
            print("Success. Size is: {} which is larger than: {}".format(buffer_size, minimum_buffer_size))
            return 0
        else:
            print("Fail. Size is: {} which is smaller than: {}".format(buffer_size, minimum_buffer_size))
            return 1
    except Exception as e:
        print("Unable to open fifo, error: {}".format(str(e)))
        return 2


def main():
    args = parse_args()
    return test_fifo(args.pipe_name, args.minimum_buffer_size)


if __name__ == '__main__':
    sys.exit(main())
