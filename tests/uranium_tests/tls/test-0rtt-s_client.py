#!/usr/bin/env python3
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

import subprocess
import sys
import os
import shlex
import h2_early_decode
import argparse


def main():
    parser = argparse.ArgumentParser(description='Process some args.')
    parser.add_argument('-p', '--ats-port', type=int, dest='ats_port', required=True, help='ATS port number')
    parser.add_argument('-v', '--http-version', type=str, dest='http_ver', choices=['h1', 'h2'], required=True, help='HTTP version')
    parser.add_argument('-t', '--test-name', type=str, dest='test_name', required=True, help='Name of the test to run')
    parser.add_argument('-r', '--run-dir', type=str, dest='run_dir', required=True, help='Path to the autest run directory')
    parser.add_argument('-s', '--server-name', type=str, dest='sni', required=False, help='Server Name')
    args = parser.parse_args()

    sess_file_path = os.path.join(args.run_dir, 'sess.dat')
    early_data_file_path = os.path.join(args.run_dir, 'early_{0}_{1}.txt'.format(args.http_ver, args.test_name))

    if args.sni != '' and args.sni is not None:
        sni_str = '-servername {0}'.format(args.sni)
    else:
        sni_str = ''

    if args.http_ver == 'h2':
        alpn_str = '-alpn h2'
    else:
        alpn_str = ''
    s_client_cmd_1 = shlex.split(
        f'openssl s_client -connect 127.0.0.1:{args.ats_port} -tls1_3 -quiet -sess_out {sess_file_path} {sni_str} {alpn_str}')
    s_client_cmd_2 = shlex.split(
        f'openssl s_client -connect 127.0.0.1:{args.ats_port} -tls1_3 -quiet -sess_in {sess_file_path} -early_data {early_data_file_path} {sni_str} {alpn_str}'
    )

    create_sess_proc = subprocess.Popen(
        s_client_cmd_1, env=os.environ.copy(), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output = create_sess_proc.communicate(input=bytes(b'GET / HTTP/1.0\r\n\r\n'), timeout=1)[0]
    except subprocess.TimeoutExpired:
        create_sess_proc.kill()
        output = create_sess_proc.communicate()[0]

    reuse_sess_proc = subprocess.Popen(
        s_client_cmd_2, env=os.environ.copy(), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output = reuse_sess_proc.communicate(timeout=1)[0]
    except subprocess.TimeoutExpired:
        reuse_sess_proc.kill()
        output = reuse_sess_proc.communicate()[0]

    if args.http_ver == 'h2':
        lines = output.split(bytes('\n', 'utf-8'))
        data = b''
        for line in lines:
            line += b'\n'
            if line.startswith(bytes('Connecting to', 'utf-8')) or \
                    line.startswith(bytes('SSL_connect:', 'utf-8')) or \
                    line.startswith(bytes('SSL3 alert', 'utf-8')) or \
                    bytes('Can\'t use SSL_get_servername', 'utf-8') in line:
                continue
            data += line
        d = h2_early_decode.Decoder()
        frames = d.decode(data)
        for frame in frames:
            print(frame)
    else:
        print(output.decode('utf-8'))

    exit(0)


if __name__ == '__main__':
    main()
