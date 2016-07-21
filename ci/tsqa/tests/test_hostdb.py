'''
Test hostdb
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

import os
import requests
import time
import logging
import socket
import SocketServer

import contextlib
import dnslib
import dnslib.server

import tsqa.test_cases
import helpers

log = logging.getLogger(__name__)


@contextlib.contextmanager
def kill_dns(dns_server):
    ''' Temporarily kill the dns server
    '''
    dns_server.stop()
    yield
    dns_server.start_thread()


class StubDNSResolver(object):
    '''Resolver to serve defined responses from `response_dict` or return SOA
    '''
    def __init__(self, responses):
        self.responses = responses
        self.resp_headers = {}

    def resolve(self, request, handler):
        reply = request.reply()
        for q in request.questions:
            qname = str(q.get_qname())
            if qname in self.responses:
                for resp in self.responses[qname]:
                    reply.add_answer(resp)
            else:
                reply.add_answer(dnslib.server.RR(
                    q.get_qname(),
                    rtype=dnslib.QTYPE.SOA,
                    ttl=1,
                    rdata=dnslib.dns.SOA(
                        'nameserver.local',
                        q.get_qname(),
                    ),
                ))
        for k, v in self.resp_headers.iteritems():
            if k == 'rcode':
                reply.header.set_rcode(v)
                print 'setting rcode'
            else:
                log.warning('Unsupported header sent to StubDNSResolver %s' % k)
        return reply


class EchoServerIpHandler(SocketServer.BaseRequestHandler):
    """
    A subclass of RequestHandler which will return a connection uuid
    """

    def handle(self):
        # Receive the data in small chunks and retransmit it
        while True:
            data = self.request.recv(4096).strip()
            if data:
                log.debug('Sending data back to the client')
            else:
                log.debug('Client disconnected')
                break
            resp = ('HTTP/1.1 200 OK\r\n'
                    'Content-Length: 0\r\n'
                    'Content-Type: text/html; charset=UTF-8\r\n'
                    'Connection: keep-alive\r\n'
                    'X-Server-Ip: {server_ip}\r\n'
                    'X-Server-Port: {server_port}\r\n'
                    'X-Client-Ip: {client_ip}\r\n'
                    'X-Client-Port: {client_port}\r\n'
                    '\r\n'.format(
                        server_ip=self.request.getsockname()[0],
                        server_port=self.request.getsockname()[1],
                        client_ip=self.request.getpeername()[0],
                        client_port=self.request.getpeername()[1],
                    ))
            self.request.sendall(resp)


class TestHostDBBadResolvConf(helpers.EnvironmentCase):
    '''
    Test that ATS can handle an empty resolv_conf
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.dns.resolv_conf': '/tmp/non_existant_file',
            'proxy.config.url_remap.remap_required': 0,

        })

    def test_working(self):
        ret = requests.get('http://trafficserver.readthedocs.org',
                           proxies=self.proxies,
                           )
        self.assertEqual(ret.status_code, 502)


class TestHostDBPartiallyFailedDNS(helpers.EnvironmentCase):
    '''
    Tests for how hostdb handles when there is one failed and one working resolver
    '''
    @classmethod
    def setUpEnv(cls, env):
        # TODO: Fix this!
        # This intermittently fails on Jenkins (such as https://ci.trafficserver.apache.org/job/tsqa-master/387/testReport/test_hostdb/TestHostDBPartiallyFailedDNS/test_working/)
        # we aren't sure if this is a failure of ATS or just a race on jenkins (since its slow)
        raise helpers.unittest.SkipTest()

        resolv_conf_path = os.path.join(env.layout.prefix, 'resolv.conf')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.dns.resolv_conf': resolv_conf_path,
            'proxy.config.url_remap.remap_required': 0,

        })

        with open(resolv_conf_path, 'w') as fh:
            fh.write('nameserver 1.1.1.0\n')  # some non-existant nameserver
            fh.write('nameserver 8.8.8.8\n')  # some REAL nameserver

    def test_working(self):
        start = time.time()
        ret = requests.get('http://trafficserver.readthedocs.org',
                           proxies=self.proxies,
                           )
        self.assertLess(time.time() - start, self.configs['records.config']['CONFIG']['proxy.config.hostdb.lookup_timeout'])
        self.assertEqual(ret.status_code, 200)


class TestHostDBFailedDNS(helpers.EnvironmentCase):
    '''
    Tests for how hostdb handles when there is no reachable resolver
    '''
    @classmethod
    def setUpEnv(cls, env):
        resolv_conf_path = os.path.join(env.layout.prefix, 'resolv.conf')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.dns.resolv_conf': resolv_conf_path,
            'proxy.config.url_remap.remap_required': 0,

        })

        with open(resolv_conf_path, 'w') as fh:
            fh.write('nameserver 1.1.1.0\n')  # some non-existant nameserver

    def test_lookup_timeout(self):
        start = time.time()
        ret = requests.get('http://some_nonexistant_domain',
                           proxies=self.proxies,
                           )
        self.assertGreater(time.time() - start, self.configs['records.config']['CONFIG']['proxy.config.hostdb.lookup_timeout'])
        self.assertEqual(ret.status_code, 502)
        self.assertIn('ATS', ret.headers['server'])


class TestHostDBHostsFile(helpers.EnvironmentCase, tsqa.test_cases.HTTPBinCase):
    '''
    Tests for hostdb's host-file implementation
    '''
    @classmethod
    def setUpEnv(cls, env):
        cls.hosts_file_path = os.path.join(env.layout.prefix, 'hosts')
        with open(cls.hosts_file_path, 'w') as fh:
            fh.write('127.0.0.1 local\n')
            fh.write('127.0.0.2 local2\n')

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 2,
            'proxy.config.url_remap.remap_required': 1,
            'proxy.config.http.connect_attempts_max_retries': 1,
            'proxy.config.hostdb.host_file.interval': 1,
            'proxy.config.hostdb.host_file.path': cls.hosts_file_path,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'hostdb',
        })
        # create a socket server
        cls.socket_server = tsqa.endpoint.SocketServerDaemon(EchoServerIpHandler)
        cls.socket_server.start()
        cls.socket_server.ready.wait()
        cls.configs['remap.config'].add_line('map http://local/ http://local:{0}/'.format(cls.socket_server.port))
        cls.configs['remap.config'].add_line('map http://local2/ http://local2:{0}/'.format(cls.socket_server.port))
        cls.configs['remap.config'].add_line('map http://local3/ http://local3:{0}/'.format(cls.socket_server.port))

    def test_basic(self):
        '''
        Test basic fnctionality of hosts files
        '''
        # TODO add stat, then wait for the stat to increment
        time.sleep(2)  # wait for the continuation to load the hosts file
        ret = requests.get(
            'http://local/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)
        self.assertEqual('127.0.0.1', ret.headers['X-Server-Ip'])

        ret = requests.get(
            'http://local2/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)
        self.assertEqual('127.0.0.2', ret.headers['X-Server-Ip'])

    def test_reload(self):
        '''
        Test that changes to hosts file get loaded within host_file.interval
        '''
        # TODO add stat, then wait for the stat to increment
        time.sleep(2)  # wait for the continuation to load the hosts file
        ret = requests.get(
            'http://local3/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 502)

        with open(self.hosts_file_path, 'a') as fh:
            fh.write('127.0.0.3 local3\n')

        # TODO add stat, then wait for the stat to increment, with a timeout
        time.sleep(2)

        ret = requests.get(
            'http://local3/get',
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)
        self.assertEqual('127.0.0.3', ret.headers['X-Server-Ip'])


class TestHostDB(helpers.EnvironmentCase, tsqa.test_cases.HTTPBinCase):
    @classmethod
    def setUpEnv(cls, env):
        cls.dns_sock = socket.socket (socket.AF_INET, socket.SOCK_DGRAM)
        cls.dns_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        cls.dns_sock.bind(('', 0))  # bind to all interfaces on an ephemeral port
        dns_port = cls.dns_sock.getsockname()[1]

        # set up dns resolver
        cls.responses = {
            'www.foo.com.': dnslib.server.RR.fromZone("foo.com. 1 A 127.0.0.1"),
            'www.stale_for.com.': dnslib.server.RR.fromZone("foo.com. 1 A 127.0.0.1"),
        }

        cls.dns_server = dnslib.server.DNSServer(
            StubDNSResolver(cls.responses),
            port=dns_port,
            address="localhost",
        )
        cls.dns_server.start_thread()

        cls.hosts_file_path = os.path.join(env.layout.prefix, 'resolv')
        with open(cls.hosts_file_path, 'w') as fh:
            fh.write('nameserver 127.0.0.1:{0}\n'.format(dns_port))

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 1,
            'proxy.config.url_remap.remap_required': 0,
            'proxy.config.http.connect_attempts_max_retries': 1,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'hostdb',
            'proxy.config.dns.resolv_conf': os.path.join(env.layout.prefix, 'resolv'),
            'proxy.config.hostdb.serve_stale_for': 2,
            'proxy.config.hostdb.ttl_mode': 0,
            'proxy.config.http_ui_enabled': 3,
        })

        cls.configs['remap.config'].add_line('map /_hostdb/ http://{hostdb}')

    def _hostdb_entries(self):
        # mapping of name -> entries
        ret = {}
        showall_ret = requests.get('http://127.0.0.1:{0}/_hostdb/showall?format=json'.format(
            self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
        ), timeout=1).json()

        for item in showall_ret:
            ret[item['hostname']] = item

        return ret

    def test_dns(self):
        '''Test that DNS lookups end up in hostdb as we expect
        '''
        # TODO: remove
        self.test_basic()
        print self._hostdb_entries()

        # test something with a LARGE number of entries
        zone_parts = []
        # TODO: fix this, right now there is `#define DNS_MAX_ADDRS 35` which
        # controls how many work-- we should make this configurable
        # 30 works, once you pass 35 some records are missing, and at some point
        # you start getting garbage (50 for example) and at some point (100) it
        # seems to crash
        NUM_RECORDS = 2
        for x in xrange(0, NUM_RECORDS):
            zone_parts.append("www.huge.com. 1 A 127.0.0.{0}".format(x + 1))
        self.responses['www.huge.com.'] = dnslib.server.RR.fromZone('\n'.join(zone_parts))

        ret = requests.get(
            'http://www.huge.com:{0}/get'.format(self.http_endpoint.address[1]),
            proxies=self.proxies,
        )
        #self.assertEqual(ret.status_code, 200)

        for item in self._hostdb_entries()['www.huge.com']['rr_records']:
            print item['ip']

        self.assertEqual(len(self._hostdb_entries()['www.huge.com']['rr_records']), NUM_RECORDS)


    def test_basic(self):
        '''
        Test basic fnctionality of resolver
        '''

        # test one that works
        ret = requests.get(
            'http://www.foo.com:{0}/get'.format(self.http_endpoint.address[1]),
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)

        # check one that doesn't exist
        ret = requests.get(
            'http://www.bar.com:{0}/get'.format(self.http_endpoint.address[1]),
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 502)

    def test_serve_stail_for(self):
        start = time.time()
        ret = requests.get(
            'http://www.stale_for.com:{0}/get'.format(self.http_endpoint.address[1]),
            proxies=self.proxies,
        )
        self.assertEqual(ret.status_code, 200)
        # mark the DNSServer down
        with kill_dns(self.dns_server):
            timeout_at = time.time() + 10
            end_working = None
            end = None
            count = 0

            while time.time() < timeout_at:
                ret = requests.get(
                    'http://www.stale_for.com:{0}/get'.format(self.http_endpoint.address[1]),
                    proxies=self.proxies,
                )
                count += 1
                if ret.status_code != 200:
                    end = time.time()
                    break
                else:
                    end_working = time.time()
                time.sleep(0.5)
            # ensure that it was for at least 2 seconds
            print end_working - start
            self.assertTrue(end_working - start >= 2)
            # TODO: Fix this!
            # for whatever reason the failed DNS response is taking ~3.5s to timeout
            # even though the hostdb.lookup_timeout is set to 1 (meaning it should be ~1s)
            #print end - end_working
            #self.assertTrue(end - start >= 2)


class TestHostDBSRV(helpers.EnvironmentCase):
    '''Tests for SRV records within hostdb

        Tests:
            - SRV record
                - port overriding
                - http/https lookups
            - fallback to non SRV
    '''
    SS_CONFIG = {
        '_http._tcp.www.foo.com.': lambda: tsqa.endpoint.SocketServerDaemon(EchoServerIpHandler),
        '_https._tcp.www.foo.com.': lambda: tsqa.endpoint.SSLSocketServerDaemon(
            EchoServerIpHandler,
            helpers.tests_file_path('cert.pem'),
            helpers.tests_file_path('key.pem'),
        ),
    }

    @classmethod
    def setUpEnv(cls, env):
        cls.dns_sock = socket.socket (socket.AF_INET, socket.SOCK_DGRAM)
        cls.dns_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        cls.dns_sock.bind(('', 0))  # bind to all interfaces on an ephemeral port
        dns_port = cls.dns_sock.getsockname()[1]

        # set up dns resolver
        cls.responses = {
            'www.foo.com.': dnslib.server.RR.fromZone("foo.com. 1 A 127.0.0.3\nfoo.com. 1 A 127.0.0.2"),
            'www.stale_for.com.': dnslib.server.RR.fromZone("foo.com. 1 A 127.0.0.1"),
        }

        cls.dns_server = dnslib.server.DNSServer(
            StubDNSResolver(cls.responses),
            port=dns_port,
            address="localhost",
        )
        cls.dns_server.start_thread()

        cls.ssl_port = tsqa.utils.bind_unused_port()[1]
        cls.configs['records.config']['CONFIG']['proxy.config.http.server_ports'] += ' {0}:ssl'.format(cls.ssl_port)

        cls.configs['records.config']['CONFIG'].update({
            'proxy.config.http.response_server_enabled': 2,  # only add server headers when there weren't any
            'proxy.config.hostdb.lookup_timeout': 1,
            'proxy.config.http.connect_attempts_max_retries': 1,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'hostdb',
            'proxy.config.dns.resolv_conf': os.path.join(env.layout.prefix, 'resolv'),
            'proxy.config.hostdb.serve_stale_for': 2,
            'proxy.config.hostdb.ttl_mode': 0,
            'proxy.config.http_ui_enabled': 3,
            'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns_port),
            'proxy.config.srv_enabled': 1,
        })

        cls.configs['ssl_multicert.config'].add_line('dest_ip=* ssl_cert_name={0}'.format(
            helpers.tests_file_path('rsa_keys/www.test.com.pem'),
        ))

        y = -1
        for name, factory in cls.SS_CONFIG.iteritems():
            y += 1
            ss_dns_results = []
            for x in xrange(0, 3):
                ss = factory()
                ss.start()
                ss.ready.wait()
                ss_dns_results.append(dnslib.server.RR(
                    name,
                    dnslib.dns.QTYPE.SRV,
                    rdata = dnslib.dns.SRV(
                        priority=10,
                        weight=10,
                        port=ss.port,
                        target='127.0.{0}.{1}.'.format(y, x + 1),  # note: NUM_REALS must be < 253
                    ),
                    ttl=1,
                ))
            cls.responses[name] = ss_dns_results

        cls.configs['remap.config'].add_line('map http://www.foo.com/ http://www.foo.com/')
        cls.configs['remap.config'].add_line('map https://www.foo.com/ https://www.foo.com/')
        cls.configs['remap.config'].add_line('map /_hostdb/ http://{hostdb}')

    def _hostdb_entries(self):
        # mapping of name -> entries
        ret = {}
        showall_ret = requests.get('http://127.0.0.1:{0}/_hostdb/showall?format=json'.format(
            self.configs['records.config']['CONFIG']['proxy.config.http.server_ports']
        ), timeout=1)
        return showall_ret.text

        for item in showall_ret:
            ret[item['hostname']] = item

        return ret

    def test_https(self):
        '''Test https SRV lookups

        we expect the SRV lookup to get different hosts, but otherwise act the same
        '''
        time.sleep(1)
        expected_set = set([d.rdata.port for d in self.responses['_https._tcp.www.foo.com.']])

        actual_set = set()
        for x in xrange(0, 10):
            # test one that works
            ret = requests.get(
                'https://localhost:{0}/'.format(self.ssl_port),
                headers={'Host': 'www.foo.com'},
                verify=False,  # self signed certs, don't bother verifying
            )
            self.assertEqual(ret.status_code, 200)
            actual_set.add(int(ret.headers['X-Server-Port']))

        self.assertEqual(expected_set, actual_set)

    def test_ports(self):
        '''Test port functionality of SRV responses

        SRV responses include ports-- so we want to ensure that we are correctly
        overriding the port based on the response
        '''
        time.sleep(1)
        expected_set = set([d.rdata.port for d in self.responses['_http._tcp.www.foo.com.']])

        actual_set = set()
        for x in xrange(0, 10):
            # test one that works
            ret = requests.get(
                'http://www.foo.com/',
                proxies=self.proxies,
            )
            self.assertEqual(ret.status_code, 200)
            actual_set.add(int(ret.headers['X-Server-Port']))

        self.assertEqual(expected_set, actual_set)

    # TODO: fix, seems broken...
    @helpers.unittest.expectedFailure
    def test_priority(self):
        '''Test port functionality of SRV responses

        SRV responses include ports-- so we want to ensure that we are correctly
        overriding the port based on the response
        '''
        time.sleep(3)  # TODO: clear somehow? waiting for expiry is lame

        NUM_REQUESTS = 10
        orig_responses = self.responses['_http._tcp.www.foo.com.']
        try:
            self.responses['_http._tcp.www.foo.com.'][0].rdata.priority=1

            request_distribution = {}
            for x in xrange(0, NUM_REQUESTS):
                # test one that works
                ret = requests.get(
                    'http://www.foo.com/',
                    proxies=self.proxies,
                )
                self.assertEqual(ret.status_code, 200)
                port = int(ret.headers['X-Server-Port'])
                if port not in request_distribution:
                    request_distribution[port] = 0
                request_distribution[port] += 1

            # since one has higher priority, we want to ensure that it got all requests
            self.assertEqual(
                request_distribution[self.responses['_http._tcp.www.foo.com.'][0].rdata.port],
                NUM_REQUESTS,
            )

        finally:
            self.responses['_http._tcp.www.foo.com.'] = orig_responses

    # TODO: fix, seems broken...
    @helpers.unittest.expectedFailure
    def test_weight(self):
        '''Test port functionality of SRV responses

        SRV responses include ports-- so we want to ensure that we are correctly
        overriding the port based on the response
        '''
        time.sleep(3)  # TODO: clear somehow? waiting for expiry is lame

        NUM_REQUESTS = 100
        orig_responses = self.responses['_http._tcp.www.foo.com.']
        try:
            self.responses['_http._tcp.www.foo.com.'][0].rdata.weight=100

            request_distribution = {}
            for x in xrange(0, NUM_REQUESTS):
                # test one that works
                ret = requests.get(
                    'http://www.foo.com/',
                    proxies=self.proxies,
                )
                self.assertEqual(ret.status_code, 200)
                port = int(ret.headers['X-Server-Port'])
                if port not in request_distribution:
                    request_distribution[port] = 0
                request_distribution[port] += 1

            # since the first one has a significantly higher weight, we expect it to
            # take ~10x the traffic of the other 2
            self.assertTrue(
                request_distribution[self.responses['_http._tcp.www.foo.com.'][0].rdata.port] >
                (NUM_REQUESTS / len(self.responses['_http._tcp.www.foo.com.'])) * 2,
                'Expected significantly more traffic on {0} than the rest: {1}'.format(
                    self.responses['_http._tcp.www.foo.com.'][0].rdata.port,
                    request_distribution,
                ),
            )

        finally:
            self.responses['_http._tcp.www.foo.com.'] = orig_responses
