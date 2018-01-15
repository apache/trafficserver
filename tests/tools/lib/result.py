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

import sys


class TermColors:
    ''' Collection of colors for printing out to terminal '''
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    ENDC = '\033[0m'


ignoredFields = {'age', 'set-cookie', 'server', 'date', 'last-modified',
                 'via', 'expires', 'cache-control', 'vary', 'connection'}  # all lower case


class Result(object):
    ''' Result encapsulates the result of a single session replay '''

    def __init__(self, test_name, expected_response, received_response, recv_resp_body=None):
        ''' expected_response and received_response can be any datatype the caller wants as long as they are the same datatype '''
        self._test_name = test_name
        self._expected_response = expected_response
        self._received_response = received_response
        self._received_response_body = recv_resp_body

    def getTestName(self):
        return self._test_name

    def getResultBool(self):
        return self._expected_response == self._received_response

    def getRespBody(self):
        if self._received_response_body:
            return self._received_response_body
        else:
            return ""

    def Compare(self, received_dict, expected_dict, src=None):
        global ignoredFields
        # print("RECEIVED")
        # print(received_dict)
        # print("RECIEVED CACHE CONTROL")
        # print(received_dict['Cache-Control'.lower()])
        # print("EXPECTED")
        # print(expected_dict)
        try:
            for key in received_dict:
                # print(key)
                if key.lower() in expected_dict and key.lower() not in ignoredFields:
                    # print("{0} ==? {1}".format(expected_dict[key.lower()],received_dict[key]))
                    if received_dict[key.lower()] != expected_dict[key.lower()]:
                        print("{0}Difference in the field \"{1}\": \n received:\n{2}\n expected:\n{3}{4}".format(
                            TermColors.FAIL, key, received_dict[key], expected_dict[key], TermColors.ENDC))
                        return False
                    if key.lower() == 'content-length' and self._received_response_body:
                        if int(received_dict[key.lower()]) != len(self._received_response_body):
                            print("{0}Difference in received content length and actual body length \ncontent-length: {1} \nbody: {2}\nbody length: {3}{4}".format(
                                TermColors.FAIL, received_dict[key.lower()], self._received_response_body, len(
                                    self._received_response_body, TermColors.ENDC)
                            ))
                            return False

        except:
            e = sys.exc_info()
            if src:
                print("In {0}: ".format(src), end='')
            print("Error in comparing key ", e, key, "expected", expected_dict[key.lower()], "received", received_dict[key])
            return False
        return True

    def getResult(self, received_dict, expected_dict, colorize=False):
        global ignoredFields
        retval = False
        ''' Return a nicely formatted result string with color if requested '''
        if self.getResultBool() and self.Compare(received_dict, expected_dict, self._test_name):
            if colorize:
                outstr = "{0}PASS{1}".format(
                    TermColors.OKGREEN, TermColors.ENDC)

            else:
                outstr = "PASS"

            retval = True

        else:
            if colorize:
                outstr = "{0}FAIL{1}: expected {2}, received {3}, session file: {4}".format(
                    TermColors.FAIL, TermColors.ENDC, self._expected_response, self._received_response, self._test_name)

            else:
                outstr = "FAIL: expected {0}, received {1}".format(
                    self._expected_response, self._received_response)

        return (retval, outstr)
