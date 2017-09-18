#!/usr/bin/env python
import glob
import io
import json
import os
import sys
import tcp_client

import asyncio

variables = json.loads(sys.argv[1])
host = '127.0.0.1'
port = variables['port']

CLIENT_TO_SERVER_KEY = 'Client -> TrafficServer'
SERVER_TO_CLIENT_KEY = 'Client <- TrafficServer'
output = io.StringIO()

socket = tcp_client.get_socket()
socket.connect((host, port))

loop = asyncio.get_event_loop()


def send_data(data, halfClose=False):
    socket.sendall(data.encode())
    if isLastStep:
        socket.shutdown(socket.SHUT_WR)

def receive_data(socket, writable):
    while True:
        socket.settimeout(0.)
        output = socket.recv(4096)  # suggested bufsize from docs.python.org
        if len(output) <= 0:
            break
        else:
            writable.write(output.decode())

print('try')
try:
    for filename in glob.iglob('**/*.test.json', recursive=True):
        print(filename)
        testObj = json.load(open(filename, 'r'))
        for testKey in testObj.keys():
            print(testKey)
            for txn in testObj[testKey]['transactions']:
                print(txn)
                for idx, step in enumerate(txn['steps']):
                    print(step)
                    if CLIENT_TO_SERVER_KEY in step:
                        data = step[CLIENT_TO_SERVER_KEY]
                        print('sending data')
                        isLastStep = (idx + 1) == len(txn['steps'])
                        send_data(data, halfClose=isLastStep)
                        print('done sending')
                    elif SERVER_TO_CLIENT_KEY in step:
                        print('receiving data')
                        receive_data(socket, output)

                print('done sending all steps')
finally:
    sys.stdout.write(output.getvalue())
    output.close()
