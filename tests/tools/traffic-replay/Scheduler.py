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

import time
import random
import json
from multiprocessing import Process, Queue, current_process
from progress.bar import Bar
import sessionvalidation.sessionvalidation as sv
import WorkerTask
import time

    
def LaunchWorkers(path,nProcess,proxy,replay_type, nThread):
    ms1=time.time()
    s = sv.SessionValidator(path)
    sessions = s.getSessionList()
    sessions.sort(key=lambda session: session._timestamp)
    Processes=[]
    Qsize = 25000 #int (1.5 * len(sessions)/(nProcess))
    QList=[Queue(Qsize) for i in range(nProcess)]
    print("Dropped {0} sessions for being malformed. Number of correct sessions {1}".format(len(s.getBadSessionList()),len(sessions)))
    print(range(nProcess))
    OutputQ=Queue();
    #======================================== Pre-load queues
    for session in sessions:
        #if nProcess == 1:
        #    QList[0].put(session)
        #else:            
        QList[random.randint(0,nProcess-1)].put(session)
        #if QList[0].qsize() > 10 :
        #    break
    #=============================================== Launch Processes
    print("size",QList[0].qsize())
    for i in range(nProcess):
        QList[i].put('STOP')
    for i in range(nProcess):
        p=Process(target=WorkerTask.worker, args=[QList[i],OutputQ,proxy,replay_type, nThread])
        p.daemon=False
        Processes.append(p);
        p.start()
    
    for p in Processes:
        p.join()
    ms2=time.time()
    print("OK enough, it is time to exit, running time in seconds", (ms2-ms1)) 
