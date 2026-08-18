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
"""Proxy Verifier server service for procedural Uranium tests."""

from __future__ import annotations

from ..process import ManagedProcess
from ..utils import tcp_open
from .process_service import ProcessService


class VerifierServer(ProcessService):
    """A pytest-owned Proxy Verifier server."""

    def __init__(self, process: ManagedProcess, http_port: int, https_port: int) -> None:
        """Create a Proxy Verifier server service.

        :param process: Managed verifier-server process.
        :param http_port: Clear-text HTTP listener port, or zero when disabled.
        :param https_port: HTTPS listener port, or zero when disabled.
        """

        super().__init__(process, reject_expression="Violation")
        self._http_port = http_port
        self._https_port = https_port

    @property
    def http_port(self) -> int:
        return self._http_port

    @property
    def https_port(self) -> int:
        return self._https_port

    def start(self) -> None:
        super().start()
        port = self.http_port or self.https_port
        self._process.wait_until(lambda: tcp_open(port), 10, f"Proxy Verifier listener on 127.0.0.1:{port}")
