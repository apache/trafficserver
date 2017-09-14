#!/usr/bin/env python
import glob
import io
import json
import os
import sys
import tcp_client

variables = json.loads(sys.argv[1])
host = '127.0.0.1'
port = variables['port']

CLIENT_TO_SERVER_KEY = 'Client -> TrafficServer'
output = io.StringIO()
print('try')
try:
    for filename in glob.iglob('**/*.test.json', recursive=True):
        print(filename)
        testObj = json.load(open(filename, 'r'))
        for testKey in testObj.keys():
            print(testKey)
            for txn in testObj[testKey]['transactions']:
                print(txn)
                for step in txn['steps']:
                    print(step)
                    if CLIENT_TO_SERVER_KEY in step:
                        data = step[CLIENT_TO_SERVER_KEY]
                        print(data)
                        tcp_client.tcp_client(host, port, data, output)
                        print('done sending')
finally:
    sys.stdout.write(output.getvalue())
    output.close()
