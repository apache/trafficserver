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

def main():
    ats_port = sys.argv[1]
    sess_file_path = os.path.join(sys.argv[2], 'sess.dat')
    early_data_file_path = os.path.join(sys.argv[2], 'early.txt')

    s_client_cmd_1 = shlex.split('openssl s_client -connect 127.0.0.1:{0} -tls1_3 -state -quiet -sess_out {1}'.format(ats_port, sess_file_path))
    s_client_cmd_2 = shlex.split('openssl s_client -connect 127.0.0.1:{0} -tls1_3 -state -quiet -sess_in {1} -early_data {2}'.format(ats_port, sess_file_path, early_data_file_path))

    create_sess_proc = subprocess.Popen(s_client_cmd_1, env=os.environ.copy(), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output = create_sess_proc.communicate(timeout=1)[0]
    except subprocess.TimeoutExpired:
        create_sess_proc.kill()
        output = create_sess_proc.communicate()[0]
    print(output.decode('utf-8'))

    reuse_sess_proc = subprocess.Popen(s_client_cmd_2, env=os.environ.copy(), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output = reuse_sess_proc.communicate(timeout=1)[0]
    except subprocess.TimeoutExpired:
        reuse_sess_proc.kill()
        output = reuse_sess_proc.communicate()[0]
    print(output.decode('utf-8'))

    exit(0)

if __name__ == '__main__':
    main()
