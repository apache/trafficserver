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

import sessionvalidation.request as request
import sessionvalidation.response as response

class Transaction(object):
    ''' Tranaction encapsulates a single UA transaction '''

    def getRequest(self):
        return self._request

    def getResponse(self):
        return self._response

    def __repr__(self):
        return "<Transaction {{'uuid': {0}, 'request': {1}, 'response': {2}}}>".format(
            self._uuid, self._request, self._response
        )

    def __init__(self, request, response, uuid):
        self._request = request
        self._response = response
        self._uuid = uuid
