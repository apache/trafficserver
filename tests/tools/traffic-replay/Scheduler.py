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
import sessionvalidation.sessionvalidation as sv
import WorkerTask
import time


def LaunchWorkers(path, nProcess, proxy, replay_type, nThread):
    ms1 = time.time()
    s = sv.SessionValidator(path, allow_custom=True)
    sessions = s.getSessionList()
    sessions.sort(key=lambda session: session._timestamp)
    Processes = []
    Qsize = 25000  # int (1.5 * len(sessions)/(nProcess))
    QList = [Queue(Qsize) for i in range(nProcess)]
    print("Dropped {0} sessions for being malformed. Number of correct sessions {1}".format(
        len(s.getBadSessionList()), len(sessions)))
    print(range(nProcess))
    OutputQ = Queue()
    #======================================== Pre-load queues
    for session in sessions:
        if replay_type == 'mixed':
            if nProcess < 2:
                raise ValueError("For mixed replay type, there should be at least 2 processes.")
            # odd Qs for SSL sessions, even Qs for nonSSL sessions
            num = random.randint(0, nProcess - 1)

            # get the first transaction in each session, which is indictive if session is over SSL or not
            if "https" in session.returnFirstTransaction().getRequest().getHeaders():
                # spin until we get an odd number
                while num & 1 == 0:
                    num = random.randint(0, nProcess - 1)
            else:
                # nonSSL sessions get put here into even Qs
                while num & 1 == 1:
                    num = random.randint(0, nProcess - 1)

            QList[num].put(session)
        else:
            # if nProcess == 1:
            #    QList[0].put(session)
            # else:
            QList[random.randint(0, nProcess - 1)].put(session)
            # if QList[0].qsize() > 10 :
            #    break
    #=============================================== Launch Processes
    # for i in range(nProcess):
    #     QList[i].put('STOP')
    for i in range(nProcess):
        QList[i].put('STOP')

        if replay_type == 'mixed':
            if i & 1:  # odd/SSL
                p = Process(target=WorkerTask.worker, args=[QList[i], OutputQ, proxy, 'ssl', nThread])
            else:  # even/nonSSL
                p = Process(target=WorkerTask.worker, args=[QList[i], OutputQ, proxy, 'nossl', nThread])
        else:
            p = Process(target=WorkerTask.worker, args=[QList[i], OutputQ, proxy, replay_type, nThread])

        p.daemon = False
        Processes.append(p)
        p.start()

    for p in Processes:
        p.join()
    ms2 = time.time()
    print("OK enough, it is time to exit, running time in seconds", (ms2 - ms1))
