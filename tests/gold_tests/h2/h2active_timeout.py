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

from hyper import HTTPConnection
import hyper
import argparse
import time


def makerequest(port, active_timeout):
    hyper.tls._context = hyper.tls.init_context()
    hyper.tls._context.check_hostname = False
    hyper.tls._context.verify_mode = hyper.compat.ssl.CERT_NONE

    conn = HTTPConnection('localhost:{0}'.format(port), secure=True)

    try:
        # delay after sending the first request
        # so the H2 session active timeout triggers
        # Then the next request should fail
        req_id = conn.request('GET', '/')
        time.sleep(active_timeout)
        response = conn.get_response(req_id)
        req_id = conn.request('GET', '/')
        response = conn.get_response(req_id)
    except Exception:
        print('CONNECTION_TIMEOUT')
        return

    print('NO_TIMEOUT')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", "-p",
                        type=int,
                        help="Port to use")
    parser.add_argument("--delay", "-d",
                        type=int,
                        help="Time to delay in seconds")
    args = parser.parse_args()
    makerequest(args.port, args.delay)


if __name__ == '__main__':
    main()
