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


def getResponseString(response):
    typestr = str(type(response))
    if typestr.find('HTTP20') != -1:
        string = "HTTP/2 {0}\r\n".format(response.status)
    else:
        string = "HTTP {0}\r\n".format(response.status)
    string += 'date: ' + response.headers.get('date')[0].decode('utf-8') + "\r\n"
    string += 'server: ' + response.headers.get('Server')[0].decode('utf-8') + "\r\n"
    return string


def makerequest(port):
    hyper.tls._context = hyper.tls.init_context()
    hyper.tls._context.check_hostname = False
    hyper.tls._context.verify_mode = hyper.compat.ssl.CERT_NONE

    conn = HTTPConnection('localhost:{0}'.format(port), secure=True)

    # Fetch the object twice so we know at least one time comes from cache
    # Exploring timing options
    sites = ['/bigfile', '/bigfile']
    responses = []
    request_ids = []
    for site in sites:
        request_id = conn.request('GET', url=site)
        request_ids.append(request_id)

    # get responses
    for req_id in request_ids:
        response = conn.get_response(req_id)
        body = response.read()
        cl = response.headers.get('Content-Length')[0]
        print("Content length = {}\r\n".format(int(cl)))
        print("Body length = {}\r\n".format(len(body)))
        error = 0
        if chr(body[0]) != 'a':
            error = 1
            print("First char {}".format(body[0]))
        i = 1
        while i < len(body) and not error:
            error = chr(body[i]) != 'b'
            if error:
                print("bad char {} at {}".format(body[i], i))
            i = i + 1
        if not error:
            print("Content success\r\n")
        else:
            print("Content fail\r\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", "-p",
                        type=int,
                        help="Port to use")
    args = parser.parse_args()
    makerequest(args.port)


if __name__ == '__main__':
    main()
