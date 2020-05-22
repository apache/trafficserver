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


def getResponseString(response):
    typestr = str(type(response))
    if typestr.find('HTTP20') != -1:
        string = "HTTP/2 {0}\r\n".format(response.status)
    else:
        string = "HTTP {0}\r\n".format(response.status)
    string += 'date: ' + response.headers.get('date')[0].decode('utf-8') + "\r\n"
    string += 'server: ' + response.headers.get('Server')[0].decode('utf-8') + "\r\n"
    return string


def makerequest(port, _url):
    hyper.tls._context = hyper.tls.init_context()
    hyper.tls._context.check_hostname = False
    hyper.tls._context.verify_mode = hyper.compat.ssl.CERT_NONE

    conn = HTTPConnection('localhost:{0}'.format(port), secure=True)

    sites = {'/'}
    request_ids = []
    for _ in sites:
        request_id = conn.request('GET', url=_url)
        request_ids.append(request_id)

    # get responses
    for req_id in request_ids:
        response = conn.get_response(req_id)
        body = response.read()
        print(getResponseString(response))
        print(body.decode('utf-8'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", "-p",
                        type=int,
                        help="Port to use")
    parser.add_argument("--url", "-u",
                        type=str,
                        help="url")
    args = parser.parse_args()
    makerequest(args.port, args.url)


if __name__ == '__main__':
    main()
