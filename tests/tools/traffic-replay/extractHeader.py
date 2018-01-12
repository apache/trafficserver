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

import sessionvalidation


def extract_txn_req_method(headers):
    ''' Extracts the HTTP request method from the header in a string format '''
    line = (headers.split('\r\n'))[0]
    return (line.split(' '))[0]


def extract_host(headers):
    ''' Returns the host header from the given headers '''
    lines = headers.split('\r\n')
    for line in lines:
        if 'Host:' in line:
            return line.split(' ')[1]
    return "notfound"


def responseHeader_to_dict(header):
    headerFields = header.split('\r\n', 1)[1]
    fields = headerFields.split('\r\n')
    header = [x for x in fields if (x != u'')]
    headers = {}
    for line in header:
        split_here = line.find(":")
        # append multiple headers into a single string
        if line[:split_here].lower() in headers:
            headers[line[:split_here].lower()] += ", {0}".format(line[(split_here + 1):].strip())
        else:
            headers[line[:split_here].lower()] = line[(split_here + 1):].strip()

    return headers


def header_to_dict(header):
    ''' Convert a HTTP header in string format to a python dictionary
    Returns a dictionary of header values
    '''
    header = header.split('\r\n')
    header = [x for x in header if (x != u'')]
    headers = {}
    for line in header:
        if 'GET' in line or 'POST' in line or 'HEAD' in line:     # ignore initial request line
            continue

        split_here = line.find(":")
        headers[line[:split_here]] = line[(split_here + 1):].strip()

    return headers


def extract_GET_path(headers):
    ''' Extracts the HTTP request URL from the header in a string format '''
    line = (headers.split('\r\n'))[0]
    return (line.split(' '))[1]
