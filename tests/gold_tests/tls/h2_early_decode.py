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
A simple tool to decode http2 frames for 0-rtt testing.
'''

import hpack
import sys


class Http2FrameDefs:

    RESERVE_BIT_MASK = 0x7fffffff

    DATA_FRAME = 0x00
    HEADERS_FRAME = 0x01
    PRIORITY_FRAME = 0x02
    RST_STREAM_FRAME = 0x03
    SETTINGS_FRAME = 0x04
    PUSH_PROMISE_FRAME = 0x05
    PING_FRAME = 0x06
    GOAWAY_FRAME = 0x07
    WINDOW_UPDATE_FRAME = 0x08
    CONTINUATION_FRAME = 0x09

    FRAME_TYPES = {
        DATA_FRAME: 'DATA',
        HEADERS_FRAME: 'HEADERS',
        PRIORITY_FRAME: 'PRIORITY',
        RST_STREAM_FRAME: 'RST_STREAM',
        SETTINGS_FRAME: 'SETTINGS',
        PUSH_PROMISE_FRAME: 'PUSH_PROMISE',
        PING_FRAME: 'PING',
        GOAWAY_FRAME: 'GOAWAY',
        WINDOW_UPDATE_FRAME: 'WINDOW_UPDATE',
        CONTINUATION_FRAME: 'CONTINUATION'
    }

    SETTINGS_HEADER_TABLE_SIZE = 0x01
    SETTINGS_ENABLE_PUSH = 0x02
    SETTINGS_MAX_CONCURRENT_STREAMS = 0x03
    SETTINGS_INITIAL_WINDOW_SIZE = 0x04
    SETTINGS_MAX_FRAME_SIZE = 0x05
    SETTINGS_MAX_HEADER_LIST_SIZE = 0x06

    SETTINGS_ID = {
        SETTINGS_HEADER_TABLE_SIZE: 'HEADER_TABLE_SIZE',
        SETTINGS_ENABLE_PUSH: 'ENABLE_PUSH',
        SETTINGS_MAX_CONCURRENT_STREAMS: 'MAX_CONCURRENT_STREAMS',
        SETTINGS_INITIAL_WINDOW_SIZE: 'INITIAL_WINDOW_SIZE',
        SETTINGS_MAX_FRAME_SIZE: 'MAX_FRAME_SIZE',
        SETTINGS_MAX_HEADER_LIST_SIZE: 'MAX_HEADER_LIST_SIZE'
    }

    RST_STREAM_NO_ERROR = 0x0
    RST_STREAM_PROTOCOL_ERROR = 0x1
    RST_STREAM_INTERNAL_ERROR = 0x2
    RST_STREAM_FLOW_CONTROL_ERROR = 0x3
    RST_STREAM_SETTINGS_TIMEOUT = 0x4
    RST_STREAM_STREAM_CLOSED = 0x5
    RST_STREAM_FRAME_SIZE_ERROR = 0x6
    RST_STREAM_REFUSED_STREAM = 0x7
    RST_STREAM_CANCEL = 0x8
    RST_STREAM_COMPRESSION_ERROR = 0x9
    RST_STREAM_CONNECT_ERROR = 0xa
    RST_STREAM_ENHANCE_YOUR_CALM = 0xb
    RST_STREAM_INADEQUATE_SECURITY = 0xc
    RST_STREAM_HTTP_1_1_REQUIRED = 0xd

    RST_STREAM_ERROR_CODES = {
        RST_STREAM_NO_ERROR: 'NO_ERROR',
        RST_STREAM_PROTOCOL_ERROR: 'PROTOCOL_ERROR',
        RST_STREAM_INTERNAL_ERROR: 'INTERNAL_ERROR',
        RST_STREAM_FLOW_CONTROL_ERROR: 'FLOW_CONTROL_ERROR',
        RST_STREAM_SETTINGS_TIMEOUT: 'SETTINGS_TIMEOUT',
        RST_STREAM_STREAM_CLOSED: 'STREAM_CLOSED',
        RST_STREAM_FRAME_SIZE_ERROR: 'FRAME_SIZE_ERROR',
        RST_STREAM_REFUSED_STREAM: 'REFUSED_STREAM',
        RST_STREAM_CANCEL: 'CANCEL',
        RST_STREAM_COMPRESSION_ERROR: 'COMPRESSION_ERROR',
        RST_STREAM_CONNECT_ERROR: 'CONNECT_ERROR',
        RST_STREAM_ENHANCE_YOUR_CALM: 'ENHANCE_YOUR_CALM',
        RST_STREAM_INADEQUATE_SECURITY: 'INADEQUATE_SECURITY',
        RST_STREAM_HTTP_1_1_REQUIRED: 'HTTP_1_1_REQUIRED'
    }


class Http2Frame:
    def __init__(self, length, frame_type, flags, stream_id):
        self.length = length
        self.frame_type = frame_type
        self.flags = flags
        self.stream_id = stream_id
        self.payload = None
        self.decode_error = None
        return

    def add_payload(self, payload):
        self.payload = payload
        return

    def read_data(self):
        if self.frame_type == Http2FrameDefs.DATA_FRAME:
            return '\n' + self.payload.decode('utf-8')
        else:
            return '\nError: Frame type mismatch: {0}'.format(Http2FrameDefs.FRAME_TYPES[self.frame_type])

    def read_headers(self):
        if self.frame_type == Http2FrameDefs.HEADERS_FRAME:
            try:
                decoder = hpack.Decoder()
                decoded_data = decoder.decode(self.payload)
                output_str = ''
                for header in decoded_data:
                    output_str += '\n'
                    for each in header:
                        output_str += each + ' '
            except hpack.exceptions.InvalidTableIndex:
                output_str = self.payload.hex()
                output_str += '\nWarning: Decode failed: Invalid table index (not too important)'
            return output_str
        else:
            return '\nError: Frame type mismatch: {0}'.format(Http2FrameDefs.FRAME_TYPES[self.frame_type])

    def read_rst_stream(self):
        if self.frame_type == Http2FrameDefs.RST_STREAM_FRAME:
            error_code = int(self.payload.hex(), 16)
            return '\nError Code = {0}'.format(Http2FrameDefs.RST_STREAM_ERROR_CODES[error_code])
        else:
            return '\nError: Frame type mismatch: {0}'.format(Http2FrameDefs.FRAME_TYPES[self.frame_type])

    def read_settings(self):
        if self.frame_type == Http2FrameDefs.SETTINGS_FRAME:
            settings_str = ''
            for i in range(0, self.length, 6):
                settings_id = int(self.payload[i:i + 2].hex(), 16)
                settings_val = int(self.payload[i + 2:i + 6].hex(), 16)
                settings_str += '\n{0} = {1}'.format(Http2FrameDefs.SETTINGS_ID[settings_id], settings_val)
            return settings_str
        else:
            return '\nError: Frame type mismatch: {0}'.format(Http2FrameDefs.FRAME_TYPES[self.frame_type])

    def read_goaway(self):
        if self.frame_type == Http2FrameDefs.GOAWAY_FRAME:
            last_stream_id = int(self.payload[0:4].hex(), 16) & Http2FrameDefs.RESERVE_BIT_MASK
            error_code = int(self.payload[4:8].hex(), 16)
            debug_data = self.payload[8:].hex()
            return '\nLast Stream ID = 0x{0:08x}\nError Code = 0x{1:08x}\nDebug Data = {2}'.format(
                last_stream_id, error_code, debug_data
            )
        else:
            return '\nError: Frame type mismatch: {0}'.format(Http2FrameDefs.FRAME_TYPES[self.frame_type])

    def read_window_update(self):
        if self.frame_type == Http2FrameDefs.WINDOW_UPDATE_FRAME:
            window_size_increment = int(self.payload.hex(), 16) & Http2FrameDefs.RESERVE_BIT_MASK
            return '\nWindow Size Increment = {0}'.format(window_size_increment)
        else:
            return '\nError: Frame type mismatch: {0}'.format(Http2FrameDefs.FRAME_TYPES[self.frame_type])

    def print_payload(self):
        if self.frame_type == Http2FrameDefs.DATA_FRAME:
            return self.read_data()
        elif self.frame_type == Http2FrameDefs.HEADERS_FRAME:
            return self.read_headers()
        elif self.frame_type == Http2FrameDefs.RST_STREAM_FRAME:
            return self.read_rst_stream()
        elif self.frame_type == Http2FrameDefs.SETTINGS_FRAME:
            return self.read_settings()
        elif self.frame_type == Http2FrameDefs.GOAWAY_FRAME:
            return self.read_goaway()
        elif self.frame_type == Http2FrameDefs.WINDOW_UPDATE_FRAME:
            return self.read_window_update()
        else:
            return self.payload.hex()

    def print(self):
        output = 'Length: {0}\nType: {1}\nFlags: {2}\nStream ID: {3}\nPayload: {4}\n'.format(
            self.length, Http2FrameDefs.FRAME_TYPES[self.frame_type], self.flags, self.stream_id, self.print_payload()
        )
        if self.decode_error is not None:
            output += self.decode_error + '\n'
        return output

    def __str__(self):
        return self.print()


class Decoder:
    def read_frame_header(self, data):
        frame = Http2Frame(
            length=int(data[0:3].hex(), 16),
            frame_type=int(data[3:4].hex(), 16),
            flags=int(data[4:5].hex(), 16),
            stream_id=int(data[5:9].hex(), 16) & Http2FrameDefs.RESERVE_BIT_MASK
        )
        return frame

    def decode(self, data):
        temp_data = data
        frames = []
        while len(temp_data) >= 9:
            frame_header = temp_data[0:9]
            frame = self.read_frame_header(frame_header)
            if frame.length > len(temp_data[9:]):
                frame.decode_error = 'Error: Payload length greater than data: {0} > {1}'.format(frame.length, len(temp_data[9:]))
                frame.add_payload(temp_data[9:])
                frames.append(frame)
            else:
                frame.add_payload(temp_data[9:9 + frame.length])
                frames.append(frame)
                temp_data = temp_data[9 + frame.length:]
        return frames


def main():
    # input file is output from openssl s_client.
    # sample command to get this output:
    # openssl s_client -bind 127.0.0.1:61991 -connect 127.0.0.1:61992 -tls1_3
    # -quiet -sess_out /home/duke/Dev/ats-test/sess.dat -sess_in
    # /home/duke/Dev/ats-test/sess.dat -early_data ./gold_tests/tls/early2.txt
    # >! _sandbox/tls_0rtt_server/early2_out.txt 2>&1

    if len(sys.argv) < 2:
        print('Error: No input file to decode.')
        exit(1)

    lines = None
    with open(sys.argv[1], 'rb') as in_file:
        lines = in_file.readlines()

    data = b''
    for line in lines:
        if line.startswith(bytes('SSL_connect:', 'utf-8')) or \
                line.startswith(bytes('SSL3 alert', 'utf-8')) or \
                bytes('Can\'t use SSL_get_servername', 'utf-8') in line:
            continue
        data += line

    d = Decoder()
    frames = d.decode(data)
    for frame in frames:
        print(frame)
    exit(0)


if __name__ == "__main__":
    main()
