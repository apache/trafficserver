#!/usr/bin/env python3
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

import psutil
import argparse
import sys


def count_threads(ts_path, etnet_threads, accept_threads, task_threads, aio_threads):

    for p in psutil.process_iter(['name', 'cwd', 'threads']):

        # Find the pid corresponding to the ats process we started in autest.
        # It needs to match the process name and the binary path.
        # If autest can expose the pid of the process this is not needed anymore.
        if p.name() == '[TS_MAIN]' and p.cwd() == ts_path:

            etnet_check = set()
            accept_check = set()
            task_check = set()
            aio_check = set()

            for t in p.threads():

                # Get the name of the thread.
                thread_name = psutil.Process(t.id).name()

                if thread_name.startswith('[ET_NET'):

                    # Get the id of this thread and check if it's in range.
                    etnet_id = int(thread_name.split(' ')[1][:-1])
                    if etnet_id >= etnet_threads:
                        sys.stderr.write('Too many ET_NET threads created.\n')
                        return 2
                    elif etnet_id in etnet_check:
                        sys.stderr.write('ET_NET thread with duplicate thread id created.\n')
                        return 3
                    else:
                        etnet_check.add(etnet_id)

                elif thread_name.startswith('[ACCEPT'):

                    # Get the id of this thread and check if it's in range.
                    accept_id = int(thread_name.split(' ')[1].split(':')[0])
                    if accept_id >= accept_threads:
                        sys.stderr.write('Too many ACCEPT threads created.\n')
                        return 5
                    else:
                        accept_check.add(accept_id)

                elif thread_name.startswith('[ET_TASK'):

                    # Get the id of this thread and check if it's in range.
                    task_id = int(thread_name.split(' ')[1][:-1])
                    if task_id >= task_threads:
                        sys.stderr.write('Too many ET_TASK threads created.\n')
                        return 7
                    elif task_id in task_check:
                        sys.stderr.write('ET_TASK thread with duplicate thread id created.\n')
                        return 8
                    else:
                        task_check.add(task_id)

                elif thread_name.startswith('[ET_AIO'):

                    # Get the id of this thread and check if it's in range.
                    aio_id = int(thread_name.split(' ')[1].split(':')[0])
                    if aio_id >= aio_threads:
                        sys.stderr.write('Too many ET_AIO threads created.\n')
                        return 10
                    else:
                        aio_check.add(aio_id)

            # Check the size of the sets, must be equal to the expected size.
            if len(etnet_check) != etnet_threads:
                sys.stderr.write('Expected ET_NET threads: {0}, found: {1}.\n'.format(etnet_threads, len(etnet_check)))
                return 4
            elif len(accept_check) != accept_threads:
                sys.stderr.write('Expected ACCEPT threads: {0}, found: {1}.\n'.format(accept_threads, len(accept_check)))
                return 6
            elif len(task_check) != task_threads:
                sys.stderr.write('Expected ET_TASK threads: {0}, found: {1}.\n'.format(task_threads, len(task_check)))
                return 9
            elif len(aio_check) != aio_threads:
                sys.stderr.write('Expected ET_AIO threads: {0}, found: {1}.\n'.format(aio_threads, len(aio_check)))
                return 11
            else:
                return 0

    # Return 1 if no pid is found to match the ats process.
    return 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--ts-path', type=str, dest='ts_path', help='path to traffic_server binary', required=True)
    parser.add_argument(
        '-e', '--etnet-threads', type=int, dest='etnet_threads', help='expected number of ET_NET threads', required=True)
    parser.add_argument(
        '-a', '--accept-threads', type=int, dest='accept_threads', help='expected number of ACCEPT threads', required=True)
    parser.add_argument(
        '-t', '--task-threads', type=int, dest='task_threads', help='expected number of TASK threads', required=True)
    parser.add_argument('-c', '--aio-threads', type=int, dest='aio_threads', help='expected number of AIO threads', required=True)
    args = parser.parse_args()
    exit(count_threads(args.ts_path, args.etnet_threads, args.accept_threads, args.task_threads, args.aio_threads))


if __name__ == '__main__':
    main()
