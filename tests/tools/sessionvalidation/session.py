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
import sessionvalidation.transaction as transaction

class Session(object):
    ''' Session encapsulates a single user session '''

    def getTransactionList(self):
        ''' Returns a list of transaction objects '''
        return self._transaction_list

    def getTransactionIter(self):
        ''' Returns an iterator of transaction objects '''
        return iter(self._transaction_list)

    def __repr__(self):
        return "<Session {{'filename': {0}, 'version': {1}, 'timestamp: {2}, 'encoding': {3}, 'transaction_list': {4}}}>".format(
                  self._filename, self._version, self._timestamp, self._encoding, repr(self._transaction_list)
            )

    def __init__(self, filename, version, timestamp, transaction_list, encoding=None):
        self._filename = filename
        self._version = version
        self._timestamp = timestamp
        self._encoding = encoding
        self._transaction_list = transaction_list
