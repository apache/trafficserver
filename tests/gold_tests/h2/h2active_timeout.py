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

import json
from hyper import HTTPConnection
import hyper
import argparse
import time


def makerequest(port):
    hyper.tls._context = hyper.tls.init_context()
    hyper.tls._context.check_hostname = False
    hyper.tls._context.verify_mode = hyper.compat.ssl.CERT_NONE

    conn = HTTPConnection('localhost:{0}'.format(port), secure=True)

    active_timeout = 3
    request_interval = 0.1
    loop_cnt = int((active_timeout + 2) / request_interval)
    for i in range(loop_cnt):
        try:
            conn.request('GET', '/')
            time.sleep(request_interval)
        except:
            print('CONNECTION_TIMEOUT')
            return

    print('NO_TIMEOUT')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", "-p",
                        type=int,
                        help="Port to use")
    args = parser.parse_args()
    makerequest(args.port)


if __name__ == '__main__':
    main()
