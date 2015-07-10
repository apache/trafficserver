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

from OpenSSL import SSL
import socket
import subprocess

import helpers
import tsqa.utils

import os


# helper function to get the path of a program.
def which(program):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)
    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            path = path.strip('"')
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None


class TestTLSTicketKeyRotation(helpers.EnvironmentCase):
    """
     Test TLS session resumption through session tickets and TLS ticket key rotation.
    """
    @classmethod
    def setUpEnv(cls, env):
        '''
        This function is responsible for setting up the environment for this fixture
        This includes everything pre-daemon start
        '''

        # add an SSL port to ATS
        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.enabled'] = 1
        cls.configs['records.config']['CONFIG']['proxy.config.diags.debug.tags'] = 'ssl'

        # configure SSL multicert

        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0} ssl_key_name={1} ticket_key_name={2}'.format(helpers.tests_file_path('rsa_keys/ca.crt'), helpers.tests_file_path('rsa_keys/ca.key'), helpers.tests_file_path('rsa_keys/ssl_ticket.key')))

    def start_connection(self, addr):
        '''
        Return the certificate for addr.
        '''
        ctx = SSL.Context(SSL.SSLv23_METHOD)
        # Set up client
        sock = SSL.Connection(ctx, socket.socket(socket.AF_INET, socket.SOCK_STREAM))
        sock.connect(addr)
        sock.do_handshake()

    def test_tls_ticket_resumption(self):
        '''
        Make sure the new ticket key is loaded
        '''
        addr = ('127.0.0.1', self.ssl_port)
        self.start_connection(addr)

        # openssl s_client -connect 127.0.0.1:443 -tls1 < /dev/null
        sess = os.path.join(self.environment.layout.logdir, 'sess')
        ticket_cmd = 'echo | openssl s_client -connect {0}:{1} -sess_out {2}'.format(addr[0], addr[1], sess)

        # check whether TLS session tickets are received by s_client.
        stdout, _ = tsqa.utils.run_sync_command(ticket_cmd, stdout=subprocess.PIPE, shell=True)
        ticket_exists = False
        for line in stdout.splitlines():
            text = line.strip()
            if text.startswith("TLS session ticket:"):
                ticket_exists = True
                break
        self.assertTrue(ticket_exists, "Sesssion tickets are not received")

        # check whether the session has been reused
        reused = False
        ticket_cmd = 'echo | openssl s_client -connect {0}:{1} -sess_in {2}'.format(addr[0], addr[1], sess)
        stdout, _ = tsqa.utils.run_sync_command(ticket_cmd, stdout=subprocess.PIPE, shell=True)
        for line in stdout.splitlines():
            text = line.strip()
            if text.startswith("Reused, TLSv1/SSLv3,"):
                reused = True
                break
        self.assertTrue(reused, "TLS session was not reused!")

        # negative test case. The session is not reused.
        reused = False
        ticket_cmd = 'echo | openssl s_client -connect {0}:{1}'.format(addr[0], addr[1])
        stdout, _ = tsqa.utils.run_sync_command(ticket_cmd, stdout=subprocess.PIPE, shell=True)
        for line in stdout.splitlines():
            text = line.strip()
            if text.startswith("Reused, TLSv1/SSLv3,"):
                reused = True
                break
        self.assertFalse(reused, "TLS session has been reused!")

    def test_tls_ticket_rotation(self):
        '''
        Make sure the new ticket key is loaded
        '''
        addr = ('127.0.0.1', self.ssl_port)
        self.start_connection(addr)

        '''
        openssl s_client -connect server_ip:ssl_port -tls1 < /dev/null
        '''

        # Generate and push a new ticket key
        rotate_cmd = 'openssl rand 48 -base64 > {0}'.format(helpers.tests_file_path('rsa_keys/ssl_ticket.key'))
        stdout, _ = tsqa.utils.run_sync_command(rotate_cmd, stdout=subprocess.PIPE, shell=True)

        # touch the ssl_multicert.config file
        ssl_multicert = os.path.join(self.environment.layout.sysconfdir, 'ssl_multicert.config')

        read_renewed_cmd = os.path.join(self.environment.layout.bindir, 'traffic_line') + ' -r proxy.process.ssl.total_ticket_keys_renewed'

        # Check whether the config file exists.
        self.assertTrue(os.path.isfile(ssl_multicert), ssl_multicert)
        touch_cmd = which('touch') + ' ' + ssl_multicert
        tsqa.utils.run_sync_command(touch_cmd, stdout=subprocess.PIPE, shell=True)

        count = 0
        while True:
            try:
                stdout, _ = tsqa.utils.run_sync_command(read_renewed_cmd, stdout=subprocess.PIPE, shell=True)
                old_renewed = stdout
                break
            except Exception:
                ++count
                # If we have tried 30 times and the command still failed, quit here.
                if count > 30:
                    self.assertTrue(False, "Failed to get the number of renewed keys!")

        signal_cmd = os.path.join(self.environment.layout.bindir, 'traffic_line') + ' -x'
        tsqa.utils.run_sync_command(signal_cmd, stdout=subprocess.PIPE, shell=True)

        # wait for the ticket keys to be sucked in by traffic_server.
        count = 0
        while True:
            try:
                stdout, _ = tsqa.utils.run_sync_command(read_renewed_cmd, stdout=subprocess.PIPE, shell=True)
                cur_renewed = stdout
                if old_renewed != cur_renewed:
                    break
            except Exception:
                ++count
                if count > 30:
                    self.assertTrue(False, "Failed to get the number of renewed keys!")

        # the number of ticket keys renewed has been increased.
        self.assertNotEqual(old_renewed, cur_renewed)
