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
import os

import sessionvalidation.session as session
import sessionvalidation.transaction as transaction
import sessionvalidation.request as request
import sessionvalidation.response as response

# valid_HTTP_request_methods = ['GET', 'POST', 'HEAD']
# custom_HTTP_request_methods = ['PULL']  # transaction monitor plugin for ATS may have custom methods
allowed_HTTP_request_methods = ['GET', 'POST', 'HEAD', 'PULL']
G_CUSTOM_METHODS = False
G_VERBOSE_LOG = True


def _verbose_print(msg, verbose_on=False):
    ''' Print msg if verbose_on is set to True or G_VERBOSE_LOG is set to True'''
    if verbose_on or G_VERBOSE_LOG:
        print(msg)


class SessionValidator(object):
    '''
    SessionValidator parses, validates, and exports an API for a given set of JSON sessions generated from Apache Traffic Server

    SessionValidator is initialized with a path to a directory of JSON sessions. It then automatically parses and validates all the
    session in the directory. After initialization, the user may use the provided API

    TODO :
    Provide a list of guaranteed fields for each type of object (ie a Transaction has a request and a response, a request has ...)
    '''

    def parse(self):
        '''
        Constructs Session objects from JSON files on disk and stores objects into _sessions

        All sessions missing required fields (ie. a session timestamp, a response for every request, etc) are
        dropped and the filename is stored inside _bad_sessions
        '''

        log_filenames = [os.path.join(self._json_log_dir, f) for f in os.listdir(
            self._json_log_dir) if os.path.isfile(os.path.join(self._json_log_dir, f))]

        for fname in log_filenames:
            with open(fname) as f:
                # first attempt to load the JSON
                try:
                    sesh = json.load(f)
                except:
                    self._bad_sessions.append(fname)
                    _verbose_print("Warning: JSON parse error on file={0}".format(fname))
                    print("Warning: JSON parse error on file={0}".format(fname))
                    continue

                # then attempt to extract all the required fields from the JSON
                try:
                    session_timestamp = sesh['timestamp']
                    session_version = sesh['version']
                    session_txns = list()
                    for txn in sesh['txns']:
                        # create transaction Request object
                        txn_request = txn['request']

                        txn_request_body = ''
                        if 'body' in txn_request:
                            txn_request_body = txn_request['body']
                        txn_request_obj = request.Request(txn_request['timestamp'], txn_request['headers'], txn_request_body)
                        # Create transaction Response object
                        txn_response = txn['response']
                        txn_response_body = ''
                        if 'body' in txn_response:
                            txn_response_body = txn_response['body']
                        txn_response_obj = response.Response(txn_response['timestamp'], txn_response['headers'], txn_response_body,
                                txn_response.get('options'))

                        # create Transaction object
                        txn_obj = transaction.Transaction(txn_request_obj, txn_response_obj, txn['uuid'])
                        session_txns.append(txn_obj)
                    session_obj = session.Session(fname, session_version, session_timestamp, session_txns)

                except KeyError as e:
                    self._bad_sessions.append(fname)
                    print("Warning: parse error on key={0} for file={1}".format(e, fname))
                    _verbose_print("Warning: parse error on key={0} for file={1}".format(e, fname))
                    continue

                self._sessions.append(session_obj)

    def validate(self):
        ''' Prunes out all the invalid Sessions in _sessions '''

        good_sessions = list()

        for sesh in self._sessions:
            if SessionValidator.validateSingleSession(sesh):
                good_sessions.append(sesh)
            else:
                self._bad_sessions.append(sesh._filename)

        self._sessions = good_sessions

    @staticmethod
    def validateSingleSession(sesh):
        ''' Takes in a single Session object as input, returns whether or not the Session is valid '''

        retval = True

        try:
            # first validate fields
            if not sesh._filename:
                _verbose_print("bad session filename")
                retval = False
            elif not sesh._version:
                _verbose_print("bad session version")
                retval = False
            elif float(sesh._timestamp) <= 0:
                _verbose_print("bad session timestamp")
                retval = False
            elif not bool(sesh.getTransactionList()):
                _verbose_print("session has no transaction list")
                retval = False

            # validate Transactions now
            for txn in sesh.getTransactionIter():
                if not SessionValidator.validateSingleTransaction(txn):
                    retval = False

        except ValueError as e:
            _verbose_print("most likely an invalid session timestamp")
            retval = False

        return retval

    @staticmethod
    def validateSingleTransaction(txn):
        ''' Takes in a single Transaction object as input, and returns whether or not the Transaction is valid '''

        txn_req = txn.getRequest()
        txn_resp = txn.getResponse()
        retval = True

        #valid_HTTP_request_methods = ['GET', 'HEAD', 'POST', 'PUT', 'DELETE', 'TRACE', 'OPTIONS', 'CONNECT', 'PATCH']
        # we can later uncomment the previous line to support more HTTP methods
        valid_HTTP_versions = ['HTTP/1.0', 'HTTP/1.1', 'HTTP/2.0']

        try:
            # validate request first
            if not txn_req:
                _verbose_print("no transaction request")
                retval = False
            elif txn_req.getBody() == None:
                _verbose_print("transaction body is set to None")
                retval = False
            elif float(txn_req.getTimestamp()) <= 0:
                _verbose_print("invalid transaction request timestamp")
                retval = False
            elif txn_req.getHeaders().split()[0] not in allowed_HTTP_request_methods:
                _verbose_print("invalid HTTP method for transaction {0}".format(txn_req.getHeaders().split()[0]))
                retval = False
            elif not txn_req.getHeaders().endswith("\r\n\r\n"):
                _verbose_print("transaction request headers didn't end with \\r\\n\\r\\n")
                retval = False
            elif txn_req.getHeaders().split()[2] not in valid_HTTP_versions:
                _verbose_print("invalid HTTP version in request")
                retval = False

            # if the Host header is not present and vaild we reject this transaction
            found_host = False
            for header in txn_req.getHeaders().split('\r\n'):
                split_header = header.split(' ')
                if split_header[0] == 'Host:':
                    found_host = True
                    host_header_no_space = len(split_header) == 1
                    host_header_with_space = len(split_header) == 2 and split_header[1] == ''
                    if host_header_no_space or host_header_with_space:
                        found_host = False
            if not found_host:
                print("missing host", txn_req)
                _verbose_print("transaction request Host header doesn't have specified host")
                retval = False

            # now validate response
            if not txn_resp:
                _verbose_print("no transaction response")
                retval = False
            elif txn_resp.getBody() == None:
                _verbose_print("transaction response body set to None")
                retval = False
            elif float(txn_resp.getTimestamp()) <= 0:
                _verbose_print("invalid transaction response timestamp")
                retval = False
            elif txn_resp.getHeaders().split()[0] not in valid_HTTP_versions:
                _verbose_print("invalid HTTP response header")
                retval = False
            elif not txn_resp.getHeaders().endswith("\r\n\r\n"):
                _verbose_print("transaction response headers didn't end with \\r\\n\\r\\n")
                retval = False

            # if any of the 3xx responses have bodies, then the must reject this transaction, since 3xx
            # errors by definition can't have bodies
            response_line = txn_resp.getHeaders().split('\r\n')[0]
            response_code = response_line.split(' ')[1]
            if response_code.startswith('3') and txn_resp.getBody():
                _verbose_print("transaction response was 3xx and had a body")
                retval = False

        except ValueError as e:
            _verbose_print("most likely an invalid transaction timestamp")
            retval = False

        except IndexError as e:
            _verbose_print("most likely a bad transaction header")
            retval = False

        return retval

    def getSessionList(self):
        ''' Returns the list of Session objects '''
        return self._sessions

    def getSessionIter(self):
        ''' Returns an iterator of the Session objects '''
        return iter(self._sessions)

    def getBadSessionList(self):
        ''' Returns a list of bad session filenames (list of strings) '''
        return self._bad_sessions

    def getBadSessionListIter(self):
        ''' Returns an iterator of bad session filenames (iterator of strings) '''
        return iter(self._bad_sessions)

    def __init__(self, json_log_dir, allow_custom=False):
        global valid_HTTP_request_methods
        global G_CUSTOM_METHODS
        G_CUSTOM_METHODS = allow_custom
        self._json_log_dir = json_log_dir
        self._bad_sessions = list()   # list of filenames
        self._sessions = list()       # list of _good_ session objects

        self.parse()
        self.validate()
