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
"""Classic 9.2 ACL tables and their independent pytest lifecycles."""
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
import yaml
from tools.uranium.services import ATSFactory, ServiceFactory


@dataclass(frozen=True)
class AclCase:
    """One independent ACL configuration and its expected statuses."""
    name: str
    ip_allow: str
    inline: str = ""
    named_acls: tuple[tuple[str, str], ...] = ()
    expected_responses: tuple[int | None, ...] = ()
    deactivate_ip_allow: bool = False
    proxy_protocol: bool = False


def combination_acl_cases(records: Sequence[Mapping[str, object]], *, prefix: str) -> list[AclCase]:
    """Translate a release-branch expectation table.

    :param records: Original branch table with GET and POST expectations.
    :param prefix: Stable identifier distinguishing the three tables.
    """
    cases = []
    for index, record in enumerate(records):
        if "named_acl_1" in record:
            named = (("acl_1", str(record["named_acl_1"])), ("acl_2", str(record["named_acl_2"])))
        else:
            named = (("acl", str(record["named_acl"])),) if record["named_acl"] else ()
        cases.append(
            AclCase(
                f"{prefix}-{index}", str(record["ip_allow"]), str(record.get("inline", "")), named,
                (record["GET response"], record["POST response"]), bool(record.get("deactivate_ip_allow", False))))
    return cases


def run_remap_acl(ats_factory: ATSFactory, services: ServiceFactory, case: AclCase, *, test_directory: Path) -> None:
    """Exercise one ACL without sharing ATS state with other table rows.

    :param ats_factory: Isolated proxy factory.
    :param services: Verifier process owner.
    :param case: ACL definitions and expected statuses.
    :param test_directory: Directory containing original replay templates.
    """
    if case.proxy_protocol:
        replay = test_directory / "remap_acl_get_post_allowed_pp.replay.yaml"
    else:
        document = yaml.safe_load((test_directory / "base.replay.yaml").read_text())
        expected = dict(zip(("GET", "POST"), case.expected_responses))
        for session in document["sessions"]:
            for transaction in session["transactions"]:
                status = expected[transaction["client-request"]["method"]]
                transaction["proxy-response"]["status"] = 403 if status is None else status
        replay = ats_factory.run_directory / "acl.replay.yaml"
        replay.write_text(yaml.safe_dump(document, sort_keys=False))
    origin = services.verifier_server("origin", replay)
    ats = ats_factory.create("ts", enable_cache=False, enable_tls=True, enable_proxy_protocol=True)
    ats.records.update(
        {
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|url|remap|ip-allow|proxyprotocol",
            "proxy.config.http.push_method_enabled": 1,
            "proxy.config.http.connect_ports": origin.http_port,
            "proxy.config.acl.subjects": "PROXY,PEER",
        })
    ats.ip_allow_config.add_lines(case.ip_allow)
    if case.deactivate_ip_allow:
        ats.remap_config.add_line(".deactivatefilter ip_allow")
    for name, definition in case.named_acls:
        ats.remap_config.add_line(f".definefilter {name} {definition}")
    for name, _ in case.named_acls:
        ats.remap_config.add_line(f".activatefilter {name}")
    ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.http_port} {case.inline}")
    rejected = case.expected_responses == (None, None)
    origin.start()
    ats.start()
    client = services.verifier_client(
        "client",
        replay,
        http_ports=[ats.proxy_protocol_port if case.proxy_protocol else ats.http_port],
        return_code=1 if rejected else 0,
        allow_errors=rejected)
    result = client.run()
    if rejected:
        assert result.returncode == 1, result.output
        assert "client '127.0.0.1' prohibited by ip-allow policy" in ats.diags_log.read_text(errors="replace")
