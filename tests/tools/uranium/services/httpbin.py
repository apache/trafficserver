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
"""go-httpbin service for procedural Uranium tests."""

from __future__ import annotations

from ..process import ManagedProcess
from ._service_helpers import tcp_open
from .process_service import ProcessService


class HttpBinServer(ProcessService):
    """A pytest-owned go-httpbin server."""

    def __init__(self, process: ManagedProcess, port: int) -> None:
        """Create a go-httpbin service.

        :param process: Managed go-httpbin process.
        :param port: Clear-text HTTP listener port.
        """

        super().__init__(process)
        self._port = port

    @property
    def port(self) -> int:
        return self._port

    def start(self) -> None:
        super().start()
        self._process.wait_until(lambda: tcp_open(self.port), 10, f"HTTPBin listener on 127.0.0.1:{self.port}")
