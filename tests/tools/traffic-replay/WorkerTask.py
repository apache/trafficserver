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
#import threading
import sys
from multiprocessing import current_process
import sessionvalidation.sessionvalidation as sv
import lib.result as result
from progress.bar import Bar
import extractHeader
import RandomReplay
import SSLReplay
import h2Replay
def worker(input,output,proxy,replay_type,nThread):
    #progress_bar = Bar(" Replaying sessions {0}".format(current_process().name), max=input.qsize())
        #print("playing {0}=>{1}:{2}".format(current_process().name,session._timestamp,proxy))
    if replay_type == 'random':
        RandomReplay.client_replay(input, proxy, output, nThread)
    elif replay_type == 'ssl':
        SSLReplay.client_replay(input, proxy, output,nThread)
    elif replay_type == 'h2':
        h2Replay.client_replay(input, proxy, output,nThread)
        #progress_bar.next()
    #progress_bar.finish()
    print("process{0} has exited".format(current_process().name)) 
    
