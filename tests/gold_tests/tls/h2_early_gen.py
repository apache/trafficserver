#!/usr/bin/env python3

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

'''
A simple tool to generate some raw http2 frames for 0-rtt testing.
'''

# http2 frame format:
# +-----------------------------------------------+
# |                 Length (24)                   |
# +---------------+---------------+---------------+
# |   Type (8)    |   Flags (8)   |
# +-+-------------+---------------+-------------------------------+
# |R|                 Stream Identifier (31)                      |
# +=+=============================================================+
# |                   Frame Payload (0...)                      ...
# +---------------------------------------------------------------+

import hpack
import os
import sys

H2_PREFACE = bytes.fromhex('505249202a20485454502f322e300d0a0d0a534d0d0a0d0a')

RESERVED_BIT_MASK = 0x7FFFFFFF

TYPE_HEADERS_FRAME = 0x01
TYPE_SETTINGS_FRAME = 0x04
TYPE_WINDOW_UPDATE_FRAME = 0x08

SETTINGS_HEADER_TABLE_SIZE = 0x01
SETTINGS_ENABLE_PUSH = 0x02
SETTINGS_MAX_CONCURRENT_STREAMS = 0x03
SETTINGS_INITIAL_WINDOW_SIZE = 0x04
SETTINGS_MAX_FRAME_SIZE = 0x05
SETTINGS_MAX_HEADER_LIST_SIZE = 0x06

HEADERS_FLAG_END_STREAM = 0x01
HEADERS_FLAG_END_HEADERS = 0x04
HEADERS_FLAG_END_PADDED = 0x08
HEADERS_FLAG_END_PRIORITY = 0x20

CURRENT_SCRIPT_PATH = os.path.dirname(os.path.abspath(__file__))


def encode_payload(data):
    encoder = hpack.Encoder()
    data_encoded = encoder.encode(data)
    return data_encoded


def make_frame(frame_length, frame_type, frame_flags, frame_stream_id, frame_payload):
    frame_length = bytes.fromhex('{0:06x}'.format(frame_length))
    frame_type = bytes.fromhex('{0:02x}'.format(frame_type))
    frame_flags = bytes.fromhex('{0:02x}'.format(frame_flags))
    frame_stream_id = bytes.fromhex('{0:08x}'.format(RESERVED_BIT_MASK & frame_stream_id))

    frame = frame_length + frame_type + frame_flags + frame_stream_id

    if frame_payload is not None:
        frame += frame_payload

    return frame


def make_settins_frame(ack=False, empty=False):
    payload = ''
    if not ack and not empty:
        payload += '{0:04x}{1:08x}'.format(SETTINGS_ENABLE_PUSH, 0)
        payload += '{0:04x}{1:08x}'.format(SETTINGS_MAX_CONCURRENT_STREAMS, 100)
        payload += '{0:04x}{1:08x}'.format(SETTINGS_INITIAL_WINDOW_SIZE, 1073741824)
    payload = bytes.fromhex(payload)

    frame = make_frame(
        frame_length=len(payload),
        frame_type=TYPE_SETTINGS_FRAME,
        frame_flags=1 if ack else 0,
        frame_stream_id=0,
        frame_payload=payload
    )

    return frame


def make_window_update_frame():
    payload = '{0:08x}'.format(RESERVED_BIT_MASK & 1073676289)
    payload = bytes.fromhex(payload)

    frame = make_frame(
        frame_length=len(payload),
        frame_type=TYPE_WINDOW_UPDATE_FRAME,
        frame_flags=0,
        frame_stream_id=0,
        frame_payload=payload
    )
    return frame


def make_headers_frame(method, path='', stream_id=0x01):
    headers = []
    if method == 'get':
        headers.append((':method', 'GET'))
        if path != '':
            headers.append((':path', path))
        else:
            headers.append((':path', '/early_get'))
    elif method == 'post':
        headers.append((':method', 'POST'))
        if path != '':
            headers.append((':path', path))
        else:
            headers.append((':path', '/early_post'))

    headers.extend([
        (':scheme', 'https'),
        (':authority', '127.0.0.1'),
        ('host', '127.0.0.1'),
        ('accept', '*/*')
    ])

    headers_encoded = encode_payload(headers)

    frame = make_frame(
        frame_length=len(headers_encoded),
        frame_type=TYPE_HEADERS_FRAME,
        frame_flags=HEADERS_FLAG_END_STREAM | HEADERS_FLAG_END_HEADERS,
        frame_stream_id=stream_id,
        frame_payload=headers_encoded
    )

    return frame


def make_h2_req(test):
    h2_req = H2_PREFACE
    if test == 'get' or test == 'post':
        frames = [
            make_settins_frame(ack=True),
            make_headers_frame(test)
        ]
        for frame in frames:
            h2_req += frame
    elif test == 'multi1':
        frames = [
            make_settins_frame(ack=True),
            make_headers_frame('get', '/early_multi_1', 1),
            make_headers_frame('get', '/early_multi_2', 3),
            make_headers_frame('get', '/early_multi_3', 5)
        ]
        for frame in frames:
            h2_req += frame
    elif test == 'multi2':
        frames = [
            make_settins_frame(ack=True),
            make_headers_frame('get', '/early_multi_1', 1),
            make_headers_frame('post', stream_id=3),
            make_headers_frame('get', '/early_multi_3', 5)
        ]
        for frame in frames:
            h2_req += frame
    else:
        pass
    return h2_req


def write_to_file(data, file_name):
    with open(file_name, 'wb') as out_file:
        out_file.write(data)
    return


def main():
    test = sys.argv[1]
    write_to_file(make_h2_req(test), os.path.join(CURRENT_SCRIPT_PATH, 'early_h2_{0}.txt'.format(test)))
    exit(0)


if __name__ == '__main__':
    main()
