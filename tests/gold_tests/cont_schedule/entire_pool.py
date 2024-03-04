#!/usr/bin/env python3

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
'''
A simple tool to check continuation is scheduled on an entire thread pool.
'''

import sys
import re


def main():
    log_path = sys.argv[1]
    thread_type = sys.argv[2]
    thread_num = int(sys.argv[3])
    min_count = int(sys.argv[4])
    thread_check = {}
    with open(log_path, 'r') as f:
        for line in f:
            match = re.search(rf'\[({thread_type}) (\d+)\]', line)
            if not match:
                continue
            thread_name = f'{match.group(1)} {match.group(2):0>2}'
            if thread_name not in thread_check:
                thread_check[thread_name] = 0
            thread_check[thread_name] += 1

    for thread_name in sorted(thread_check):
        if thread_check[thread_name] < min_count:
            print(f'{thread_name}: {thread_check[thread_name]} (fail)')
        else:
            print(f'{thread_name}: {thread_check[thread_name]} (pass)')

    if len(thread_check) != thread_num:
        print(f'total threads: {len(thread_check)} (fail)')
    else:
        print(f'total threads: {len(thread_check)} (pass)')

    exit(0)


if __name__ == '__main__':
    main()
