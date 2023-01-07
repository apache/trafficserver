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

import time
import threading
import requests
import argparse


def gen(slow_time):
    for _ in range(slow_time):
        yield b'a'
        time.sleep(1)


def slow_post(port, slow_time):
    requests.post('http://127.0.0.1:{0}/'.format(port, ), data=gen(slow_time))


def makerequest(port, connection_limit):
    client_timeout = 3
    for _ in range(connection_limit):
        t = threading.Thread(target=slow_post, args=(port, client_timeout + 10))
        t.daemon = True
        t.start()
    time.sleep(1)
    r = requests.get('http://127.0.0.1:{0}/'.format(port,))
    print(r.status_code)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", "-p",
                        type=int,
                        help="Port to use")
    parser.add_argument("--connectionlimit", "-c",
                        type=int,
                        help="connection limit")
    args = parser.parse_args()
    makerequest(args.port, args.connectionlimit)


if __name__ == '__main__':
    main()
