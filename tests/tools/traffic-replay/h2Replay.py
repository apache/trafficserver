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

import os
from threading import Thread
import sys
from multiprocessing import current_process
import sessionvalidation.sessionvalidation as sv
import lib.result as result
import extractHeader
import mainProcess
import json
from hyper import HTTP20Connection
from hyper.tls import wrap_socket, H2_NPN_PROTOCOLS, H2C_PROTOCOL
from hyper.common.bufsocket import BufferedSocket
import hyper
import socket
import logging
import h2
from h2.connection import H2Configuration
import threading
import Config

log = logging.getLogger(__name__)
bSTOP = False
hyper.tls._context = hyper.tls.init_context()
hyper.tls._context.check_hostname = False
hyper.tls._context.verify_mode = hyper.compat.ssl.CERT_NONE


class _LockedObject(object):
    """
    A wrapper class that hides a specific object behind a lock.

    The goal here is to provide a simple way to protect access to an object
    that cannot safely be simultaneously accessed from multiple threads. The
    intended use of this class is simple: take hold of it with a context
    manager, which returns the protected object.
    """

    def __init__(self, obj):
        self.lock = threading.RLock()
        self._obj = obj

    def __enter__(self):
        self.lock.acquire()
        return self._obj

    def __exit__(self, _exc_type, _exc_val, _exc_tb):
        self.lock.release()


class h2ATS(HTTP20Connection):

    def __init_state(self):
        """
        Initializes the 'mutable state' portions of the HTTP/2 connection
        object.

        This method exists to enable HTTP20Connection objects to be reused if
        they're closed, by resetting the connection object to its basic state
        whenever it ends up closed. Any situation that needs to recreate the
        connection can call this method and it will be done.

        This is one of the only methods in hyper that is truly private, as
        users should be strongly discouraged from messing about with connection
        objects themselves.
        """

        config1 = H2Configuration(
            client_side=True,
            header_encoding='utf-8',
            validate_outbound_headers=False,
            validate_inbound_headers=False,

        )
        self._conn = _LockedObject(h2.connection.H2Connection(config=config1))

        # Streams are stored in a dictionary keyed off their stream IDs. We
        # also save the most recent one for easy access without having to walk
        # the dictionary.
        #
        # We add a set of all streams that we or the remote party forcefully
        # closed with RST_STREAM, to avoid encountering issues where frames
        # were already in flight before the RST was processed.
        #
        # Finally, we add a set of streams that recently received data.  When
        # using multiple threads, this avoids reading on threads that have just
        # acquired the I/O lock whose streams have already had their data read
        # for them by prior threads.
        self.streams = {}
        self.recent_stream = None
        self.next_stream_id = 1
        self.reset_streams = set()
        self.recent_recv_streams = set()

        # The socket used to send data.
        self._sock = None

        # Instantiate a window manager.
        #self.window_manager = self.__wm_class(65535)

        return

    def __init__(self, host, **kwargs):
        HTTP20Connection.__init__(self, host, **kwargs)
        self.__init_state()

    def connect(self):
        """
        Connect to the server specified when the object was created. This is a
        no-op if we're already connected.

        Concurrency
        -----------

        This method is thread-safe. It may be called from multiple threads, and
        is a noop for all threads apart from the first.

        :returns: Nothing.

        """
        #print("connecting to ATS")
        with self._lock:
            if self._sock is not None:
                return
            sni = self.host
            if not self.proxy_host:
                host = self.host
                port = self.port
            else:
                host = self.proxy_host
                port = self.proxy_port

            sock = socket.create_connection((host, port))

            if self.secure:
                #assert not self.proxy_host, "Proxy with HTTPS not supported."
                sock, proto = wrap_socket(sock, sni, self.ssl_context,
                                          force_proto=self.force_proto)
            else:
                proto = H2C_PROTOCOL

            log.debug("Selected NPN protocol: %s", proto)
            assert proto in H2_NPN_PROTOCOLS or proto == H2C_PROTOCOL

            self._sock = BufferedSocket(sock, self.network_buffer_size)

            self._send_preamble()


def createDummyBodywithLength(numberOfbytes):
    if numberOfbytes == 0:
        return None
    body = 'a'
    while numberOfbytes != 1:
        body += 'b'
        numberOfbytes -= 1
    return body


def handleResponse(response, *args, **kwargs):
    print(response.status_code)
    # resp=args[0]
    #expected_output_split = resp.getHeaders().split('\r\n')[ 0].split(' ', 2)
    #expected_output = (int(expected_output_split[1]), str( expected_output_split[2]))
    #r = result.Result(session_filename, expected_output[0], response.status_code)
    # print(r.getResultString(colorize=True))
# make sure len of the message body is greater than length


def gen():
    yield 'pforpersia,champaignurbana'.encode('utf-8')
    yield 'there'.encode('utf-8')


def txn_replay(session_filename, txn, proxy, result_queue, h2conn, request_IDs):
    """ Replays a single transaction
    :param request_session: has to be a valid requests session"""
    req = txn.getRequest()
    resp = txn.getResponse()
    # Construct HTTP request & fire it off
    txn_req_headers = req.getHeaders()
    txn_req_headers_dict = extractHeader.header_to_dict(txn_req_headers)
    txn_req_headers_dict['Content-MD5'] = txn._uuid  # used as unique identifier
    if 'body' in txn_req_headers_dict:
        del txn_req_headers_dict['body']
    responseID = -1
    #print("Replaying session")
    try:
        # response = request_session.request(extractHeader.extract_txn_req_method(txn_req_headers),
        #                            'http://' + extractHeader.extract_host(txn_req_headers) + extractHeader.extract_GET_path(txn_req_headers),
        #                            headers=txn_req_headers_dict,stream=False) # making stream=False raises contentdecoding exception? kill me
        method = extractHeader.extract_txn_req_method(txn_req_headers)
        response = None
        mbody = None
        #txn_req_headers_dict['Host'] = "localhost"
        if 'Transfer-Encoding' in txn_req_headers_dict:
            # deleting the host key, since the STUPID post/get functions are going to add host field anyway, so there will be multiple host fields in the header
            # This confuses the ATS and it returns 400 "Invalid HTTP request". I don't believe this
            # BUT, this is not a problem if the data is not chunked encoded.. Strange, huh?
            #del txn_req_headers_dict['Host']
            if 'Content-Length' in txn_req_headers_dict:
                #print("ewww !")
                del txn_req_headers_dict['Content-Length']
                mbody = gen()
        if 'Content-Length' in txn_req_headers_dict:
            nBytes = int(txn_req_headers_dict['Content-Length'])
            mbody = createDummyBodywithLength(nBytes)
        if 'Connection' in txn_req_headers_dict:
            del txn_req_headers_dict['Connection']
        #str2 = extractHeader.extract_host(txn_req_headers)+ extractHeader.extract_GET_path(txn_req_headers)
        # print(str2)
        if method == 'GET':
            responseID = h2conn.request('GET', url=extractHeader.extract_GET_path(
                txn_req_headers), headers=txn_req_headers_dict, body=mbody)
            # print("get response", responseID)
            return responseID
            # request_IDs.append(responseID)
            #response = h2conn.get_response(id)
            # print(response.headers)
            # if 'Content-Length' in response.headers:
            #        content = response.read()
            #print("len: {0} received {1}".format(response.headers['Content-Length'],content))

        elif method == 'POST':
            responseID = h2conn.request('POST', url=extractHeader.extract_GET_path(
                txn_req_headers), headers=txn_req_headers_dict, body=mbody)
            print("get response", responseID)
            return responseID

        elif method == 'HEAD':
            responseID = h2conn.request('HEAD', url=extractHeader.extract_GET_path(txn_req_headers), headers=txn_req_headers_dict)
            print("get response", responseID)
            return responseID

    except UnicodeEncodeError as e:
        # these unicode errors are due to the interaction between Requests and our wiretrace data.
        # TODO fix
        print("UnicodeEncodeError exception")

    except:
        e = sys.exc_info()
        print("ERROR in requests: ", e, response, session_filename)


def session_replay(input, proxy, result_queue):
    global bSTOP
    ''' Replay all transactions in session

    This entire session will be replayed in one requests.Session (so one socket / TCP connection)'''
    # if timing_control:
    #    time.sleep(float(session._timestamp))  # allow other threads to run
    while bSTOP == False:
        for session in iter(input.get, 'STOP'):
            print(bSTOP)
            if session == 'STOP':
                print("Queue is empty")
                bSTOP = True
                break
            txn = session.returnFirstTransaction()
            req = txn.getRequest()
            # Construct HTTP request & fire it off
            txn_req_headers = req.getHeaders()
            txn_req_headers_dict = extractHeader.header_to_dict(txn_req_headers)
            with h2ATS(txn_req_headers_dict['Host'], secure=True, proxy_host=Config.proxy_host, proxy_port=Config.proxy_ssl_port) as h2conn:
                request_IDs = []
                respList = []
                for txn in session.getTransactionIter():
                    try:
                        ret = txn_replay(session._filename, txn, proxy, result_queue, h2conn, request_IDs)
                        respList.append(txn.getResponse())
                        request_IDs.append(ret)
                        #print("txn return value is ",ret)
                    except:
                        e = sys.exc_info()
                        print("ERROR in replaying: ", e, txn.getRequest().getHeaders())
                for id in request_IDs:
                    expectedH = respList.pop(0)
                    # print("extracting",id)
                    response = h2conn.get_response(id)
                    #print("code {0}:{1}".format(response.status,response.headers))
                    response_dict = {}
                    if mainProcess.verbose:
                        for field, value in response.headers.items():
                            response_dict[field.decode('utf-8')] = value.decode('utf-8')

                        expected_output_split = expectedH.getHeaders().split('\r\n')[0].split(' ', 2)
                        expected_output = (int(expected_output_split[1]), str(expected_output_split[2]))
                        r = result.Result("", expected_output[0], response.status, response.read())
                        expected_Dict = extractHeader.responseHeader_to_dict(expectedH.getHeaders())
                        b_res, res = r.getResult(response_dict, expected_Dict, colorize=Config.colorize)
                        print(res)

                        if not b_res:
                            print("Received response")
                            print(response_dict)
                            print("Expected response")
                            print(expected_Dict)

        bSTOP = True
        #print("Queue is empty")
        input.put('STOP')
        break


def client_replay(input, proxy, result_queue, nThread):
    Threads = []
    for i in range(nThread):
        t = Thread(target=session_replay, args=[input, proxy, result_queue])
        t.start()
        Threads.append(t)

    for t1 in Threads:
        t1.join()
