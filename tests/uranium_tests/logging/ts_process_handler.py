#!/usr/bin/env python3
'''
Interact with a Traffic Server process.
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
import psutil
import signal
import sys
import traceback


class GetPidError(Exception):
    """ Raised when there was an error retrieving the specified PID."""

    def __init__(self, message):
        self.message = message
        super().__init__(self.message)


def get_desired_process(ts_identifier, get_ppid=False):
    for proc in psutil.process_iter(['cmdline']):
        commandline = ' '.join(proc.info['cmdline'])
        if '/traffic_server' in commandline and ts_identifier in commandline:
            if not get_ppid:
                return proc
            parent = proc.parent()
            with parent.oneshot():
                if 'traffic_manager' not in parent.cmdline():
                    raise GetPidError("Found a traffic server process, "
                                      "but its parent is not traffic_manager")
                return parent
    raise GetPidError("Could not find a traffic_server process")


def convert_signal_name_to_signal(signal_name):
    """
    >>> convert_signal_name_to_signal('-SIGUSR2')
    <Signals.SIGUSR2: 12>
    >>> convert_signal_name_to_signal('SIGUSR2')
    <Signals.SIGUSR2: 12>
    >>> convert_signal_name_to_signal('USR2')
    <Signals.SIGUSR2: 12>
    >>> convert_signal_name_to_signal('-USR2')
    <Signals.SIGUSR2: 12>
    >>> convert_signal_name_to_signal('KILL')
    <Signals.SIGKILL: 9>
    >>> convert_signal_name_to_signal('notasignal')
    Traceback (most recent call last):
    ...
    ValueError: Could not find a signal matching SIGnotasignal

    """
    if signal_name.startswith('-'):
        signal_name = signal_name[1:]
    if not signal_name.startswith('SIG'):
        signal_name = 'SIG' + signal_name
    for signal_value in dir(signal):
        if not signal_value.startswith('SIG'):
            continue
        if signal_name == signal_value:
            return getattr(signal, signal_value)
    raise ValueError("Could not find a signal matching {}".format(signal_name))


def parse_args():
    parser = argparse.ArgumentParser(description='Interact with a Traffic Server process')
    parser.add_argument(
        'ts_identifier', help='An identifier in the command line for the desired '
        'Traffic Server process.')
    parser.add_argument('--signal', help='Send the given signal to the process.')
    parser.add_argument(
        '--parent', action="store_true", default=False, help='Interact with the parent process of the Traffic Server process')

    return parser.parse_args()


def main():
    args = parse_args()
    try:
        process = get_desired_process(args.ts_identifier, args.parent)
    except GetPidError as e:
        print(traceback.format_exception(None, e, e.__traceback__), file=sys.stderr, flush=True)
        return 1

    if args.signal:
        signal_constant = convert_signal_name_to_signal(args.signal)
        process.send_signal(signal_constant)
    else:
        print(process.pid)
    return 0


if __name__ == '__main__':
    import doctest
    doctest.testmod()
    sys.exit(main())
