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

from pathlib import Path

import pytest

from tools.uranium.services import ATS, ATSFactory, HttpBinServer, ProceduralContext, ServiceFactory


def configure_origin(*, _services: ServiceFactory) -> HttpBinServer:
    """Create the redirect-capable go-httpbin origin.

    :param _services: Test-local services configured by the test.
    """

    return _services.httpbin("httpbin")


def configure_ats(*, _ats_factory: ATSFactory, _origin: HttpBinServer) -> ATS:
    """Configure both cache_promote remap policies.

    :param _ats_factory: Test-local ats factory configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    ats = _ats_factory.create("ts", enable_cache=True)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|cache_promote",
            "proxy.config.http.number_of_redirections": 1,
            "proxy.config.http.redirect.actions": "self:follow",
        })
    ats.plugin_config.add_line("xdebug.so --enable=x-cache,x-cache-key")
    ats.remap_config.add_line(
        f"map /test_0/ http://127.0.0.1:{_origin.port}/ "
        "@plugin=cache_promote.so @pparam=--policy=lru @pparam=--hits=2 @pparam=--buckets=15000000")
    ats.remap_config.add_line(
        f"map /test_1/ http://127.0.0.1:{_origin.port}/ "
        "@plugin=cache_promote.so @pparam=--policy=lru @pparam=--hits=2 @pparam=--buckets=15000000 "
        "@pparam=--disable-on-redirect @plugin=cachekey.so @pparam=--static-prefix=trafficserver.apache.org/443")
    return ats


def configure_client(*, _context: ProceduralContext, _directory: Path, _origin: HttpBinServer) -> Path:
    """Render the Proxy Verifier replay with the selected origin port.

    :param _context: Test-local context configured by the test.
    :param _directory: Test-local directory configured by the test.
    :param _origin: Test-local origin configured by the test.
    """

    template = (_directory / "replay/cache_promote.replay.yaml.tmpl").read_text()
    replay = _context.run_directory / "cache_promote.replay.yaml"
    replay.write_text(template.format(httpbin_port=_origin.port))
    return replay


def test_cache_promote(
    procedural_context: ProceduralContext,
    ats_factory: ATSFactory,
    services: ServiceFactory,
) -> None:
    """cache_promote applies its hit policy and redirect option.

    :param procedural_context: Procedural context used by this test step.
    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory owning support services and their cleanup.
    """
    context = procedural_context
    _directory = Path(__file__).parent
    plugin = Path(context.runtime.layout["PLUGINDIR"]) / "cache_promote.so"
    if not plugin.is_file():
        pytest.skip("cache_promote.so is required")
    _origin = configure_origin(_services=services)
    _ats = configure_ats(_ats_factory=ats_factory, _origin=_origin)

    replay = configure_client(_context=context, _directory=_directory, _origin=_origin)
    client = services.verifier_client("verifier-client", replay, http_ports=[_ats.http_port])
    _origin.start()
    _ats.start()
    client.run()
