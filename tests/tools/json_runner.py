#!/usr/bin/env python
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

import asyncio
import glob
import json
import re
import sys

variables = json.loads(sys.argv[1])
host = '127.0.0.1'
port = variables['port']

CLIENT_TO_SERVER_KEY = 'Client -> TrafficServer'
SERVER_TO_CLIENT_KEY = 'Client <- TrafficServer'

buf = b''
# This is a sketch of a method that has been added to asyncio in Python 3.5.2.
# https://github.com/python/asyncio/pull/297/files
async def readuntil(reader, delim):
    delim_len = len(delim)
    if delim_len == 0:
        raise ValueError('Cannot search for a 0-length delimiter!!')
    buf = b''
    while True:
        data = await reader.readexactly(1)
        buf += data
        if len(buf) < delim_len:
            continue
        idx = buf.find(delim)
        if idx != -1:
            # We found the delimiter.
            ret = buf[0:idx+delim_len]
            buf = buf[idx+delim_len:]
            return ret

async def send_data(writer, data, halfClose=False):
    writer.write(data.encode())
    await writer.drain()
    if halfClose:
        writer.close()

async def receive_data(reader):
    if reader.at_eof():
        reader.close()
        return
    return await readuntil(reader, b'\r\n\r\n')

async def main():
    conn_future = asyncio.open_connection(host, port)
    reader, writer = await asyncio.wait_for(conn_future, timeout=2)
    for filename in glob.iglob('**/*.test.json', recursive=True):
        testObj = json.load(open(filename, 'r'))
        for testKey in testObj.keys():
            sys.stdout.write('{}: '.format(testKey))
            for txn in testObj[testKey]['transactions']:
                for idx, step in enumerate(txn['steps']):
                    if CLIENT_TO_SERVER_KEY in step:
                        data = step[CLIENT_TO_SERVER_KEY]
                        isLastStep = (idx + 1) == len(txn['steps'])
                        await send_data(writer, data, halfClose=isLastStep)
                    elif SERVER_TO_CLIENT_KEY in step:
                        data = (await receive_data(reader)).decode()
                        if not re.compile(step[SERVER_TO_CLIENT_KEY]).match(data):
                            data_hex = ":".join("{:02x}".format(ord(c)) for c in data)
                            raise Exception("received string:\n\n{}\n'{}'\n\n"+\
                                    "did not match expected pattern:\n\n'{}'"\
                                    .format(data, data_hex , repr(step[SERVER_TO_CLIENT_KEY])))
    print('PASS')

if __name__ == '__main__':
    loop = asyncio.get_event_loop()
    loop.run_until_complete(main())
    loop.close()
