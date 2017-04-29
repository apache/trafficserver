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
import hashlib

class Request(object):
    ''' Request encapsulates a single request from the UA '''

    def getTimestamp(self):
        return self._timestamp

    def getHeaders(self):
        return self._headers

    def getBody(self):
        return self._body

    def getHeaderMD5(self):
        ''' Returns the MD5 hash of the headers

        This is used to do a unique mapping to a request/response transaction '''
        return hashlib.md5(self._headers.encode()).hexdigest()

    def __repr__(self):
        #return str(self._timestamp)
        return "<Request: {{'timestamp': {0}, 'headers': {1}, 'body': {2}}}>".format(
            str(self._timestamp), str(self._headers), str(self._body)
        )

    def __init__(self, timestamp, headers, body):
        self._timestamp = timestamp
        self._headers = headers
        self._body = body
