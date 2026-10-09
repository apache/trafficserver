'''
Verify that proxy.config.http.per_client.connection.exempt_list exempts client connections from
proxy.config.net.connections_throttle.
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
import sys

Test.Summary = __doc__
Test.SkipUnless(Condition.PluginExists('statichit.so'))

EXEMPT_LIST_VAR = 'proxy.config.http.per_client.connection.exempt_list'
OPEN = 'proxy.process.net.connections_currently_open'
EXEMPT_OPEN = 'proxy.process.net.per_client.connections_exempt_currently_open'
THROTTLED = 'proxy.process.net.connections_throttled_in'
EXEMPT = 'proxy.process.net.per_client.connections_exempt_in'
CLIENT = 'throttle_client.py'
Test.Setup.Copy(CLIENT)


def add_healthcheck(ts: 'Process') -> None:
    """Serve the health check with the statichit plugin, so that it needs no origin connection."""
    healthcheck = os.path.join(ts.Variables.CONFIGDIR, 'healthcheck.txt')
    ts.Disk.File(healthcheck, id='healthcheck', typename='ats:config')
    ts.Disk.healthcheck.AddLine('OK')
    ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1/ @plugin=statichit.so @pparam=--file-path={healthcheck}')


class ThrottleExemptTest:
    """Fill connections_throttle with connections from one address, and check which new connections are served."""

    _process_counter: int = 0

    # An accept is refused once int(connections_currently_open * 1.1) >= connections_throttle,
    # so for a throttle below 11 the held connections fill it exactly.
    _throttle: int = 5

    def __init__(self, description: str, exempt_list: str, held: str, exempt: str, accept_threads: int = 1) -> None:
        """Start Traffic Server and fill connections_throttle.

        :param exempt_list: The value of proxy.config.http.per_client.connection.exempt_list.
        :param held: The address, 127.0.0.1 or ::1, of the connections that fill the throttle.
        :param exempt: The other address, which @a exempt_list names.
        :param accept_threads: The value of proxy.config.accept_threads, which selects the accept path.
        """
        self._n = ThrottleExemptTest._process_counter
        ThrottleExemptTest._process_counter += 1
        self._description = description
        self.held = held
        self.exempt = exempt
        self._accept_threads = accept_threads
        # The expected counter values. None when a retry makes the count unknown.
        self._throttled = 0
        self._exempt_count: int | None = 0
        self._configure_trafficserver(exempt_list)
        self._fill_throttle()

    def _configure_trafficserver(self, exempt_list: str) -> None:
        """Configure Traffic Server with plain, TLS, allow-plain, and PROXY protocol ports in both families."""
        self._ts = Test.MakeATSProcess(f'ts{self._n}', enable_tls=True, enable_proxy_protocol=True)
        ts = self._ts
        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
        add_healthcheck(ts)

        allow_plain = f'allow_plain_port{self._n}'
        allow_plain_v6 = f'allow_plain_portv6{self._n}'
        Test.GetTcpPort(allow_plain, allow_plain_v6)
        self._ports = {
            'plain': (ts.Variables.port, ts.Variables.portv6),
            'tls': (ts.Variables.ssl_port, ts.Variables.ssl_portv6),
            'allow-plain': (getattr(Test.Variables, allow_plain), getattr(Test.Variables, allow_plain_v6)),
            'proxy': (ts.Variables.proxy_protocol_port, ts.Variables.proxy_protocol_portv6),
        }
        server_ports = [
            f'{ts.Variables.port}',
            f'{ts.Variables.portv6}:ipv6',
            f'{ts.Variables.ssl_port}:ssl',
            f'{ts.Variables.ssl_portv6}:ssl:ipv6',
            f'{self._ports["allow-plain"][0]}:ssl:allow-plain',
            f'{self._ports["allow-plain"][1]}:ssl:ipv6:allow-plain',
            f'{ts.Variables.proxy_protocol_port}:pp',
            f'{ts.Variables.proxy_protocol_portv6}:pp:ipv6',
        ]
        ts.Disk.records_config.update(
            {
                'proxy.config.http.server_ports': ' '.join(server_ports),
                'proxy.config.http.proxy_protocol_allowlist': '127.0.0.1,::1',
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'iocore_net_accept|statichit',
                'proxy.config.accept_threads': self._accept_threads,
                'proxy.config.net.connections_throttle': self._throttle,
                EXEMPT_LIST_VAR: exempt_list,
                # Hold the incomplete requests for the whole test.
                'proxy.config.http.accept_no_activity_timeout': 300,
                'proxy.config.http.transaction_no_activity_timeout_in': 300,
            })
        ts.Disk.diags_log.Content = Testers.ExcludesExpression('ERROR:', 'diags.log should contain no errors.')
        ts.Disk.diags_log.Content += Testers.ExcludesExpression('FATAL:', 'diags.log should not contain a fatal error.')

    def _port(self, host: str, kind: str) -> int:
        """The port of @a kind ('plain', 'tls', 'allow-plain' or 'proxy') in the family of @a host."""
        return self._ports[kind][1 if ':' in host else 0]

    def _add_run(self, description: str) -> 'TestRun':
        """Add a test run with the traffic_ctl environment for this Traffic Server."""
        tr = Test.AddTestRun(f'{self._description}: {description}')
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts
        return tr

    def _wait_counters(self) -> str:
        """A command that waits for the counters to reach their expected values, with no exempt connection open."""
        metrics = f'{OPEN} {self._throttle} {EXEMPT_OPEN} 0 {THROTTLED} {self._throttled}'
        if self._exempt_count is not None:
            metrics += f' {EXEMPT} {self._exempt_count}'
        return f'{sys.executable} {CLIENT} wait-metric {metrics}'

    def _fill_throttle(self) -> None:
        """Hold enough connections to reach connections_throttle."""
        tr = self._add_run(f'hold {self._throttle} connections from {self.held}')
        port = self._port(self.held, 'plain')
        self._holder = Test.Processes.Process(
            f'holder{self._n}', f'{sys.executable} {CLIENT} hold {self.held} {port} {self._throttle}')
        self._holder.StartBefore(self._ts)
        tr.Processes.Default.StartBefore(self._holder)
        tr.Processes.Default.Command = (
            f'{sys.executable} {CLIENT} wait-metric {OPEN} {self._throttle} {EXEMPT_OPEN} 0 {THROTTLED} 0 {EXEMPT} 0')
        tr.StillRunningAfter = self._holder

    def refused(self, host: str, description: str, kind: str = 'plain', proxy_source: str = '', retry: int = 0) -> None:
        """Check that a connection from @a host is refused and counted in connections_throttled_in.

        :param proxy_source: Claim this source address in a PROXY protocol header.
        :param retry: Retry for this many seconds, for a reload to take effect.
        """
        self._throttled += 1
        if retry:
            # The attempts that are served before the reload takes effect are counted as exempt.
            self._exempt_count = None
        tr = self._add_run(description)
        proxy_arg = f'--proxy-protocol {proxy_source}' if proxy_source else ''
        tr.Processes.Default.Command = (
            f'{sys.executable} {CLIENT} request {host} {self._port(host, kind)} refused --retry {retry} {proxy_arg} && '
            f'{self._wait_counters()}')
        tr.StillRunningAfter = self._holder

    def served(self, description: str, kind: str = 'plain', tls: bool = False, proxy_source: str = '') -> None:
        """Check that a connection from the exempt address is served, and counted as an exempt open connection.

        :param proxy_source: Claim this source address in a PROXY protocol header.
        """
        assert self._exempt_count is not None, 'A served check needs a known exempt count.'
        self._exempt_count += 1
        tr = self._add_run(description)
        tls_arg = '--tls' if tls else ''
        proxy_arg = f'--proxy-protocol {proxy_source}' if proxy_source else ''
        tr.Processes.Default.Command = (
            f'{sys.executable} {CLIENT} request {self.exempt} {self._port(self.exempt, kind)} served {tls_arg} {proxy_arg} '
            f'--metric {OPEN} --metric {EXEMPT_OPEN} && {self._wait_counters()}')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('served: HTTP/1.1 200 OK', 'The health check is served.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            f'while open: {OPEN} {self._throttle + 1}$', 'The open exempt connection is counted.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            f'while open: {EXEMPT_OPEN} 1$', 'The open exempt connection is counted as exempt.')
        tr.StillRunningAfter = self._holder

    def reload(self, exempt_list: str, description: str, log_text: str = '') -> None:
        """Set the exempt list with traffic_ctl and reload.

        :param log_text: Wait for this text in diags.log after the reload.
        """
        tr = self._add_run(f'reload {description}')
        command = f"traffic_ctl config set {EXEMPT_LIST_VAR} '{exempt_list}' && traffic_ctl config reload"
        if log_text:
            command += f' && {sys.executable} {CLIENT} wait-log {self._ts.Disk.diags_log.AbsPath} "{log_text}"'
        tr.Processes.Default.Command = command
        tr.StillRunningAfter = self._holder


def exempt_ipv6(accept_threads: int) -> None:
    """Fill the throttle from 127.0.0.1, and check ::1 over each kind of port, and a reload of the list."""
    # A network, a range, and an empty entry that match no test client, and ::1.
    t = ThrottleExemptTest(
        f'accept_threads {accept_threads}',
        '192.0.2.0/24, 198.51.100.1-198.51.100.9, ::1,',
        held='127.0.0.1',
        exempt='::1',
        accept_threads=accept_threads)
    t.refused(t.held, 'IPv4 is refused at the limit')
    t.served('::1 is served over TCP')
    t.served('::1 is served over TLS', kind='tls', tls=True)
    t.served('::1 is served over plain HTTP on an allow-plain TLS port', kind='allow-plain')
    t.served(
        '::1 is served on a PROXY protocol port that claims an address not in the list', kind='proxy', proxy_source='2001:db8::1')
    t.refused(t.held, 'IPv4 is refused on a PROXY protocol port that claims ::1', kind='proxy', proxy_source='::1')
    t.refused(t.held, 'IPv4 is still refused')

    t.reload(
        '192.0.2.1, not-an-address', 'an invalid list', f"WARNING: {EXEMPT_LIST_VAR}: 'not-an-address' is not a valid IP range")
    t.served('the previous list stays in force after an invalid reload')

    t.reload('192.0.2.1', 'a list without ::1')
    t.refused(t.exempt, '::1 is refused once the list does not name it', retry=10)


def exempt_ipv4(exempt_list: str, entry_kind: str) -> None:
    """Fill the throttle from ::1, and check that an entry of @a entry_kind matches 127.0.0.1."""
    t = ThrottleExemptTest(f'IPv4 {entry_kind}', exempt_list, held='::1', exempt='127.0.0.1')
    t.refused(t.held, 'IPv6 is refused at the limit')
    t.served(f'127.0.0.1 is served with the list {exempt_list}')
    t.refused(t.held, 'IPv6 is still refused')


def memory_limit_applies() -> None:
    """Check that the memory limit still refuses a client in the list."""
    ts = Test.MakeATSProcess('ts_memory')
    add_healthcheck(ts)
    ts.Disk.records_config.update(
        {
            EXEMPT_LIST_VAR: '::1',
            # Less than any resident set size, so every new client connection is refused once memory use is checked.
            'proxy.config.memory.max_usage': 1,
        })

    tr = Test.AddTestRun('the memory limit refuses a client in the list')
    tr.Processes.Default.StartBefore(ts)
    tr.Processes.Default.Env = ts.Env
    # Traffic Server checks memory use every 10 seconds, and serves the health check until then.
    tr.Processes.Default.Command = (
        f'{sys.executable} {CLIENT} request ::1 {ts.Variables.portv6} refused --retry 30 && '
        f'{sys.executable} {CLIENT} wait-metric {THROTTLED} 1')
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts


memory_limit_applies()

# accept_threads 1 accepts in a dedicated thread; 0 accepts on each ET_NET thread.
exempt_ipv6(accept_threads=1)
exempt_ipv6(accept_threads=0)

exempt_ipv4('127.0.0.0/8', 'CIDR network')
exempt_ipv4('127.0.0.1-127.0.0.3', 'range')
