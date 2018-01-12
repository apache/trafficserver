#!/bin/env python3
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

from __future__ import absolute_import, division, print_function
import mainProcess
import argparse
import Config

if __name__ == '__main__':

    parser = argparse.ArgumentParser()
    parser.add_argument("-type", action='store', dest='replay_type',
                        help="Replay type: ssl/random/h2/nossl/mixed (at least 2 processes needed for mixed)")
    parser.add_argument("-log_dir", type=str, help="directory of JSON replay files")
    parser.add_argument("-v", dest="verbose", help="verify response status code", action="store_true")
    parser.add_argument("-host", help="proxy/host to send the requests to", default=Config.proxy_host)
    parser.add_argument("-port", type=int, help=" The non secure port of ATS to send the request to",
                        default=Config.proxy_nonssl_port)
    parser.add_argument("-s_port", type=int, help="secure port", default=Config.proxy_ssl_port)
    parser.add_argument("-ca_cert", help="Certificate to present", default=Config.ca_certs)
    parser.add_argument("-colorize", type=str, help="specify whether to use colorize the output", default='True')

    args = parser.parse_args()

    # Let 'er loose
    #main(args.log_dir, args.hostname, int(args.port), args.threads, args.timing, args.verbose)
    Config.colorize = True if args.colorize == 'True' else False
    mainProcess.main(args.log_dir, args.replay_type, args.verbose, pHost=args.host, pNSSLport=args.port, pSSL=args.s_port)
