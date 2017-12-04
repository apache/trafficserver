# A substitute for curl - send a request and just check the status code.
# To integrate with AUTest this writes out "gold.txt" with the expected status
# then outputs the actual status so the gold check will return something useful.

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


import requests
import argparse

command_parser = argparse.ArgumentParser()
command_parser.add_argument("url", help="URL for transaction")
command_parser.add_argument("--local-proxy", type=int, metavar="port", help="Port for proxy on local host")
command_parser.add_argument("--bind", metavar="addr", help="Source IP address to bind for connection")
command_parser.add_argument("--method", metavar="method", help="HTTP request method")
args = command_parser.parse_args()

# Extra key word arguments to the actual request.
RKW_ARGS={}

if args.local_proxy :
    RKW_ARGS['proxies'] = {'http':'http://localhost:{}'.format(args.local_proxy)}

status_code = 0
try :
    s = requests.Session()
    # Ugly but by far the easiest way to set this option for the actual connection. Value is tuple (addr, port)
    if args.bind :
        a = s.get_adapter(args.url)
        if args.local_proxy :
            m = a.proxy_manager_for(RKW_ARGS['proxies']['http'])
        else:
            m = a.poolmanager
        m.connection_pool_kw['source_address'] = ( args.bind, 0 )

    if not args.method or args.method.lower() == 'get' :
        r = s.get(args.url, **RKW_ARGS)
    elif args.method.lower() == 'head' :
        r = s.head(args.url, **RKW_ARGS)
    else :
        r = s.request(args.method, args.url, **RKW_ARGS)

    status_code = r.status_code
except Exception as exp:
    print(exp)

print(status_code)
