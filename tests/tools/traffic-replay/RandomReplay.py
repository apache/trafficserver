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

import socket
import requests
import os
from threading import Thread
import sys
from multiprocessing import current_process
import sessionvalidation.sessionvalidation as sv
from collections import deque
import collections
import lib.result as result
import extractHeader
import mainProcess
import json
import gzip
import NonSSL
import SSLReplay
import h2Replay
import itertools
import random
bSTOP = False


def session_replay(input, proxy, result_queue):
    global bSTOP
    ''' Replay all transactions in session

    This entire session will be replayed in one requests.Session (so one socket / TCP connection)'''
    # if timing_control:
    #    time.sleep(float(session._timestamp))  # allow other threads to run
    while bSTOP == False:
        for session in iter(input.get, 'STOP'):
            # print(bSTOP)
            if session == 'STOP':
                print("Queue is empty")
                bSTOP = True
                break
            with requests.Session() as request_session:
                request_session.proxies = proxy
                for txn in session.getTransactionIter():
                    type = random.randint(1, 1000)
                    try:
                        if type % 3 == 0:
                            NonSSL.txn_replay(session._filename, txn, proxy, result_queue, request_session)
                        elif type % 3 == 1:
                            SSLReplay.txn_replay(session._filename, txn, proxy, result_queue, request_session)
                        elif type % 3 == 2:
                            h2Replay.txn_replay(session._filename, txn, proxy, result_queue, request_session)
                    except:
                        e = sys.exc_info()
                        print("ERROR in replaying: ", e, txn.getRequest().getHeaders())
        bSTOP = True
        #print("Queue is empty")
        input.put('STOP')
        break


def client_replay(input, proxy, result_queue, nThread):
    Threads = []
    for i in range(nThread):

        t2 = Thread(target=SSLReplay.session_replay, args=[input, proxy, result_queue])
        t = Thread(target=NonSSL.session_replay, args=[input, proxy, result_queue])
        t1 = Thread(target=h2Replay.session_replay, args=[input, proxy, result_queue])
        t2.start()
        t.start()
        t1.start()
        Threads.append(t)
        Threads.append(t2)
        Threads.append(t1)

    for t1 in Threads:
        t1.join()
