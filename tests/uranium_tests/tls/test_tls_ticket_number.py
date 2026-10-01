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

import re
import shutil

import pytest

from tools.uranium.services import ATSFactory, ServiceFactory


@pytest.mark.parametrize("ticket_number", [3, 1])
def test_tls_ticket_number(ats_factory: ATSFactory, services: ServiceFactory, ticket_number: int) -> None:
    """The global record determines the TLSv1.3 NewSessionTicket count.

    :param ats_factory: Factory owning the TLS proxy.
    :param services: Factory owning the HTTP origin.
    :param ticket_number: Number of tickets to issue, without any SNI override.
    """
    if shutil.which("openssl") is None:
        pytest.skip("openssl is required")
    server = services.origin("server")
    server.add_response(
        {
            "headers": "GET / HTTP/1.1\r\nHost: ticket-number.example.com\r\n\r\n",
            "body": ""
        }, {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "ticket number test"
        })
    ats = ats_factory.create("ts", enable_tls=True)
    ats.copy_to_ssl("ssl/server.pem")
    ats.copy_to_ssl("ssl/server.key")
    ats.copy_to_config("file.ticket")
    ats.set_ssl_multicert_yaml({"ssl_multicert": [{"dest_ip": "*", "ssl_cert_name": "server.pem", "ssl_key_name": "server.key"}]})
    ats.remap_config.add_line(f"map / http://127.0.0.1:{server.port}/")
    ats.records.update(
        {
            "proxy.config.ssl.server.session_ticket.enable": 1,
            "proxy.config.ssl.server.session_ticket.number": ticket_number,
            "proxy.config.ssl.server.ticket_key.filename": str(ats.config_directory / "file.ticket")
        })
    server.start()
    ats.start()
    result = ats.run_shell(
        "printf 'GET / HTTP/1.1\\r\\nHost: ticket-number.example.com\\r\\nConnection: close\\r\\n\\r\\n' | "
        f"openssl s_client -connect 127.0.0.1:{ats.https_port} -tls1_3 -msg -ign_eof")
    assert result.returncode == 0, result.output
    assert len(re.findall("NewSessionTicket", result.output)) == ticket_number, result.output
