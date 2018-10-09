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


class BadSession(object):
    '''
    Session encapsulates a single BAD user session. Bad meaning that for some reason the session is invalid.

    _filename is the filename of the bad JSON session
    _reason is a string with some kind of explanation on why the session was bad
    '''

    def __repr__(self):
        return "<Session {{'filename': {0}, 'reason': {1}>".format(
            self._filename, self._reason
        )

    def __init__(self, filename, reason):
        self._filename = filename
        self._reason = reason
