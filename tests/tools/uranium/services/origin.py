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
"""Microserver origin service for procedural Uranium tests."""

from __future__ import annotations

from collections.abc import Mapping
from pathlib import Path
import json
from typing import Any

from ..process import ManagedProcess
from ..utils import tcp_open
from .process_service import ProcessService


class OriginServer(ProcessService):
    """A pytest-owned microserver origin."""

    def __init__(
        self,
        process: ManagedProcess,
        port: int,
        https_port: int,
        data_directory: Path,
        address: str,
    ) -> None:
        """Create a microserver origin service.

        :param process: Managed microserver process.
        :param port: Clear-text HTTP listener port, or zero when disabled.
        :param https_port: HTTPS listener port, or zero when disabled.
        :param data_directory: Directory containing replay session files.
        :param address: Listener address passed to microserver.
        """

        super().__init__(process)
        self._port = port
        self._https_port = https_port
        self._data_directory = data_directory
        self._address = address

    @property
    def port(self) -> int:
        return self._port

    @property
    def http_port(self) -> int:
        return self._port

    @property
    def https_port(self) -> int:
        return self._https_port

    def start(self) -> None:
        super().start()
        ready_address = "127.0.0.1" if self._address == "0.0.0.0" else self._address
        ports = [port for port in (self.http_port, self.https_port) if port]
        self._process.wait_until(
            lambda: all(tcp_open(port, ready_address) for port in ports),
            10,
            f"microserver listeners on {ready_address}:{','.join(str(port) for port in ports)}",
        )

    def add_response(self, request: Mapping[str, Any], response: Mapping[str, Any], filename: str = "sessionlog.json") -> None:
        """Append a request-response transaction to a microserver session.

        :param request: Request headers, body, and optional replay options.
        :param response: Response headers, body, and optional replay options.
        :param filename: Session filename within the microserver data directory.
        """

        try:
            from trlib import Request, Response, Session, Transaction
        except ImportError as error:
            raise RuntimeError("traffic-replay is required for microserver scenarios") from error
        request_value = Request.fromRequestLine(request["headers"], request.get("body", ""), request.get("options"))
        response_value = Response.fromRequestLine(response["headers"], response.get("body", ""), response.get("options"))
        transaction = Transaction(request_value, None, response_value, None, None, None)
        path = self._data_directory / filename
        if path.exists():
            document = json.loads(path.read_text())
            document["sessions"][0]["transactions"].append(transaction.toJSON())
        else:
            document = {
                "sessions": [Session(filename, None, None, [transaction]).toJSON()],
                "meta": {
                    "version": "1.0"
                },
            }
        path.write_text(json.dumps(document))
