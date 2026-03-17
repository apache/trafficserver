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
import time


def count_threads(ts_path, etnet_threads, accept_threads, task_threads, aio_threads):

    for p in psutil.process_iter(['name', 'cwd', 'threads']):

        # Use cached info from process_iter attrs to avoid race conditions
        # where the process exits between iteration and inspection.
        try:
            proc_name = p.info.get('name', '')
            proc_cwd = p.info.get('cwd', '')
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

        # Find the pid corresponding to the ats process we started in autest.
        # It needs to match the process name and the binary path.
        # If autest can expose the pid of the process this is not needed anymore.
        if proc_name == '[TS_MAIN]' and proc_cwd == ts_path:

            etnet_check = set()
            accept_check = set()
            task_check = set()
            aio_check = set()

            try:
                threads = p.threads()
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                sys.stderr.write(f'Process {p.pid} disappeared while reading threads.\n')
                return 1

            for t in threads:

                # Get the name of the thread. The thread may have exited
                # between p.threads() and this call, so handle that.
                try:
                    thread_name = psutil.Process(t.id).name()
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    continue

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

    # No matching process found. Print diagnostic info to help debug CI failures.
    ts_main_procs = []
    for p in psutil.process_iter(['name', 'cwd']):
        try:
            if p.info.get('name') == '[TS_MAIN]':
                ts_main_procs.append(f'  pid={p.pid} cwd={p.info.get("cwd")}')
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass

    sys.stderr.write(f'No [TS_MAIN] process found with cwd={ts_path}.\n')
    if ts_main_procs:
        sys.stderr.write('Found [TS_MAIN] processes:\n' + '\n'.join(ts_main_procs) + '\n')
    else:
        sys.stderr.write('No [TS_MAIN] processes found at all.\n')
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

    max_attempts = 3
    result = 1
    for attempt in range(max_attempts):
        result = count_threads(args.ts_path, args.etnet_threads, args.accept_threads, args.task_threads, args.aio_threads)
        if result != 1:  # Only retry when process not found (exit code 1).
            break
        if attempt < max_attempts - 1:
            sys.stderr.write(f'Attempt {attempt + 1}/{max_attempts}: process not found, retrying in 2s...\n')
            time.sleep(2)

    exit(result)


if __name__ == '__main__':
    main()
