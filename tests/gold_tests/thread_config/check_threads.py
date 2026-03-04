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
import os
import sys
import time

COUNT_THREAD_WAIT_SECONDS = 10.0
COUNT_THREAD_POLL_SECONDS = 0.1


def _count_threads_once(ts_path, etnet_threads, accept_threads, task_threads, aio_threads):
    """
    Return (code, message) for a single snapshot of ATS thread state.
    """
    for p in psutil.process_iter():
        try:
            # Find the pid corresponding to the ats process we started in autest.
            # It needs to match the process name and the binary path.
            # If autest can expose the pid of the process this is not needed anymore.
            process_name = p.name()
            process_cwd = p.cwd()
            process_exe = p.exe()
            if process_cwd != ts_path:
                continue
            if process_name != '[TS_MAIN]' and process_name != 'traffic_server' and os.path.basename(
                    process_exe) != 'traffic_server':
                continue
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue

        etnet_check = set()
        accept_check = set()
        task_check = set()
        aio_check = set()

        try:
            threads = p.threads()
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            return 1, 'Could not inspect ATS process threads.'

        for t in threads:
            try:
                # Get the name of the thread.
                thread_name = psutil.Process(t.id).name()
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                # A thread can disappear while we inspect; treat as transient.
                continue

            if thread_name.startswith('[ET_NET'):

                # Get the id of this thread and check if it's in range.
                etnet_id = int(thread_name.split(' ')[1][:-1])
                if etnet_id >= etnet_threads:
                    return 2, 'Too many ET_NET threads created.'
                elif etnet_id in etnet_check:
                    return 3, 'ET_NET thread with duplicate thread id created.'
                else:
                    etnet_check.add(etnet_id)

            elif thread_name.startswith('[ACCEPT'):

                # Get the id of this thread and check if it's in range.
                accept_id = int(thread_name.split(' ')[1].split(':')[0])
                if accept_id >= accept_threads:
                    return 5, 'Too many ACCEPT threads created.'
                else:
                    accept_check.add(accept_id)

            elif thread_name.startswith('[ET_TASK'):

                # Get the id of this thread and check if it's in range.
                task_id = int(thread_name.split(' ')[1][:-1])
                if task_id >= task_threads:
                    return 7, 'Too many ET_TASK threads created.'
                elif task_id in task_check:
                    return 8, 'ET_TASK thread with duplicate thread id created.'
                else:
                    task_check.add(task_id)

            elif thread_name.startswith('[ET_AIO'):

                # Get the id of this thread and check if it's in range.
                aio_id = int(thread_name.split(' ')[1].split(':')[0])
                if aio_id >= aio_threads:
                    return 10, 'Too many ET_AIO threads created.'
                else:
                    aio_check.add(aio_id)

        # Check the size of the sets, must be equal to the expected size.
        if len(etnet_check) != etnet_threads:
            return 4, 'Expected ET_NET threads: {0}, found: {1}.'.format(etnet_threads, len(etnet_check))
        elif len(accept_check) != accept_threads:
            return 6, 'Expected ACCEPT threads: {0}, found: {1}.'.format(accept_threads, len(accept_check))
        elif len(task_check) != task_threads:
            return 9, 'Expected ET_TASK threads: {0}, found: {1}.'.format(task_threads, len(task_check))
        elif len(aio_check) != aio_threads:
            return 11, 'Expected ET_AIO threads: {0}, found: {1}.'.format(aio_threads, len(aio_check))
        else:
            return 0, ''

    return 1, 'Expected ATS process [TS_MAIN] with cwd {0}, but it was not found.'.format(ts_path)


def count_threads(
        ts_path,
        etnet_threads,
        accept_threads,
        task_threads,
        aio_threads,
        wait_seconds=COUNT_THREAD_WAIT_SECONDS,
        poll_seconds=COUNT_THREAD_POLL_SECONDS):
    deadline = time.monotonic() + wait_seconds

    # Retry on startup/transient states:
    # 1  : ATS process not found yet
    # 4/6/9/11: expected thread count not reached yet
    retry_codes = {1, 4, 6, 9, 11}

    while True:
        code, message = _count_threads_once(ts_path, etnet_threads, accept_threads, task_threads, aio_threads)
        if code == 0:
            return 0
        if code not in retry_codes or time.monotonic() >= deadline:
            sys.stderr.write(message + '\n')
            return code
        time.sleep(poll_seconds)


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
