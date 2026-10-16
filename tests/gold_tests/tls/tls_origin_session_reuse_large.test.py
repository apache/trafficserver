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

from typing import Optional

Test.Summary = 'Origin sessions are cached up to the configured ceiling, and not past it.'


class OriginSessionCeilingTest:
    """Verify the origin session cache honors proxy.config.ssl.origin_session_cache.max_session_size.

    That record sets the largest origin session ATS will cache, measured as the size of
    its ASN.1 form. A serialized session carries the origin certificate and the session
    ticket, so a large origin certificate -- or any mutual-TLS origin, whose ticket has
    to encode the client certificate in order to resume the authenticated session --
    produces a session well above the 4096 bytes this was fixed at before it became
    configurable. A session over the ceiling is dropped, and every connection to that
    origin then pays a full handshake.

    One origin, presenting a deliberately large certificate, is offered to two proxies:
    one at the default ceiling, which must cache and resume the session, and one
    configured below the session size, which must refuse it.
    """

    _origin_cert: str = 'server-large.pem'
    _origin_key: str = 'server-large.key'
    _proxy_cert: str = 'server.pem'
    _proxy_key: str = 'server.key'

    # Below the session this origin produces, so the second proxy has to reject it.
    _ceiling_below_session: int = 4096

    # The record's RECC_INT range is applied when records.yaml is parsed, but an environment
    # override is registered without that check, so the range is enforced again where the
    # value is published. These two exercise that clamp from either side.
    _env_var: str = 'PROXY_CONFIG_SSL_ORIGIN_SESSION_CACHE_MAX_SESSION_SIZE'
    _env_below_min: str = '-1'
    _env_above_max: str = '1048576'

    def __init__(self) -> None:
        """Configure the origin and every proxy scenario."""
        self._server = self._configure_server()
        self._origin = self._configure_origin()
        self._at_default = self._configure_proxy('ts_default', max_session_size=None)
        self._below_session = self._configure_proxy('ts_small', max_session_size=self._ceiling_below_session)
        self._env_low = self._configure_proxy('ts_env_low', max_session_size=None, env_override=self._env_below_min)
        self._env_high = self._configure_proxy('ts_env_high', max_session_size=None, env_override=self._env_above_max)
        self._run_cached_and_reused()
        self._run_dropped_for_size()
        self._run_env_override_below_min()
        self._run_env_override_above_max()

    def _configure_server(self) -> 'Process':
        """Configure the HTTP origin behind the TLS origin.

        :return: The origin server process.
        """
        server = Test.MakeOriginServer('server')
        request_header = {'headers': 'GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n', 'timestamp': '1469733493.993', 'body': ''}
        response_header = {
            'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n',
            'timestamp': '1469733493.993',
            'body': 'large session test'
        }
        server.addResponse('sessionlog.json', request_header, response_header)
        return server

    def _configure_origin(self) -> 'Process':
        """Configure the TLS origin, which presents the large certificate.

        :return: The origin ATS process.
        """
        ts = Test.MakeATSProcess('ts_origin', enable_tls=True)
        ts.addSSLfile(f'ssl/{self._origin_cert}')
        ts.addSSLfile(f'ssl/{self._origin_key}')
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        ts.Disk.ssl_multicert_yaml.AddLines(
            f"""
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: {self._origin_cert}
    ssl_key_name: {self._origin_key}
""".split("\n"))
        ts.Disk.records_config.update(
            {
                'proxy.config.http.cache.http': 0,
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.exec_thread.autoconfig.scale': 1.0,
                'proxy.config.ssl.server.session_ticket.enable': 1,
            })
        return ts

    def _configure_proxy(self, name: str, max_session_size: Optional[int], env_override: Optional[str] = None) -> 'Process':
        """Configure a proxy in front of the TLS origin.

        :param name: The name of the ATS process.
        :param max_session_size: The ceiling to configure, or None to leave it at the default.
        :param env_override: A value for the ceiling supplied through the environment instead
            of records.yaml, which reaches the assignment without the record's range check.
        :return: The proxy ATS process.
        """
        ts = Test.MakeATSProcess(name, enable_tls=True)
        if env_override is not None:
            ts.Env[self._env_var] = env_override
        ts.addSSLfile(f'ssl/{self._proxy_cert}')
        ts.addSSLfile(f'ssl/{self._proxy_key}')
        ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{self._origin.Variables.ssl_port}')
        ts.Disk.ssl_multicert_yaml.AddLines(
            f"""
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: {self._proxy_cert}
    ssl_key_name: {self._proxy_key}
""".split("\n"))
        records = {
            'proxy.config.http.cache.http': 0,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'ssl.origin_session_cache',
            'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
            'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
            'proxy.config.exec_thread.autoconfig.scale': 1.0,
            'proxy.config.ssl.origin_session_cache.enabled': 1,
            'proxy.config.ssl.origin_session_cache.size': 10,
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        }
        if max_session_size is not None:
            records['proxy.config.ssl.origin_session_cache.max_session_size'] = max_session_size
        ts.Disk.records_config.update(records)
        return ts

    def _two_requests(self, tr: 'TestRun', ts: 'Process') -> None:
        """Drive two requests through a proxy, so the second can resume the first.

        :param tr: The TestRun to add the client to.
        :param ts: The proxy the requests go through.
        """
        tr.MakeCurlCommandMulti(
            '{{curl}} https://127.0.0.1:{0}/ -k && {{curl}} https://127.0.0.1:{0}/ -k'.format(ts.Variables.ssl_port), ts=ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression('large session test', 'the request itself has to work')

    def _run_cached_and_reused(self) -> None:
        """At the default ceiling the large session is cached, so the second request resumes."""
        tr = Test.AddTestRun('a large origin session is cached at the default ceiling')
        self._two_requests(tr, self._at_default)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._origin)
        tr.Processes.Default.StartBefore(self._at_default)
        self._at_default.Disk.traffic_out.Content = Testers.ContainsExpression(
            'new session to origin', 'the first connection is a full handshake')
        self._at_default.Disk.traffic_out.Content += Testers.ContainsExpression(
            'reused session to origin', 'the second must resume, which requires the session to have been cached')
        self._at_default.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'Unable to save SSL session because size', 'a session under the ceiling must not be dropped for its size')
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter += self._origin
        tr.StillRunningAfter += self._at_default

    def _run_dropped_for_size(self) -> None:
        """Below the session size the same session is refused, so nothing can resume."""
        tr = Test.AddTestRun('a ceiling below the session size drops it, as configured')
        self._two_requests(tr, self._below_session)
        tr.Processes.Default.StartBefore(self._below_session)
        self._below_session.Disk.traffic_out.Content = Testers.ContainsExpression(
            'Unable to save SSL session because size', 'the session is over this proxy configured ceiling')
        self._below_session.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'reused session to origin', 'nothing was cached, so nothing can resume')
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter += self._origin

    def _run_env_override_below_min(self) -> None:
        """An environment value under the minimum is clamped up to it, and warned about.

        Clamped to 4096, which is below the session this origin produces, so the session is
        refused -- the same outcome as configuring 4096 directly.
        """
        tr = Test.AddTestRun('an environment value below the minimum is clamped')
        self._two_requests(tr, self._env_low)
        tr.Processes.Default.StartBefore(self._env_low)
        self._env_low.Disk.diags_log.Content += Testers.ContainsExpression(
            'max_session_size of -1 is outside', 'the out of range environment value has to be reported')
        self._env_low.Disk.traffic_out.Content = Testers.ContainsExpression(
            'Unable to save SSL session because size', 'clamped to the minimum, which is below this session')
        self._env_low.Disk.traffic_out.Content += Testers.ExcludesExpression(
            'reused session to origin', 'nothing was cached, so nothing can resume')
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter += self._origin

    def _run_env_override_above_max(self) -> None:
        """An environment value over the maximum is clamped down to it, and warned about.

        Without the clamp this value would reach the assignment unchanged, and a negative one
        would become SIZE_MAX -- which is what the ceiling exists to prevent, since the size is
        checked immediately before SSLSessionDup() sizes its serialization buffer from it.
        """
        tr = Test.AddTestRun('an environment value above the maximum is clamped')
        self._two_requests(tr, self._env_high)
        tr.Processes.Default.StartBefore(self._env_high)
        self._env_high.Disk.diags_log.Content += Testers.ContainsExpression(
            'max_session_size of 1048576 is outside', 'the out of range environment value has to be reported')
        self._env_high.Disk.traffic_out.Content = Testers.ContainsExpression(
            'reused session to origin', 'clamped to the maximum, which is above this session, so it still caches')
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter += self._origin


OriginSessionCeilingTest()
