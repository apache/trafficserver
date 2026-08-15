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

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import TypeAlias

import yaml

from tools.uranium.services import ATS, ATSFactory, ProcessService, ServiceFactory, VerifierServer

Filter: TypeAlias = str | tuple[str, ...]

IP_ALLOW_CONTENT = """
ip_categories:
  - name: ACME_LOCAL
    ip_addrs: 127.0.0.1
  - name: ACME_EXTERNAL
    ip_addrs: 5.6.7.8

ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: set_allow
    methods:
      - GET
"""

IP_ALLOW_OLD_ACTION = IP_ALLOW_CONTENT.replace("action: set_allow", "action: allow")


@dataclass(frozen=True)
class AclCase:
    """One independent remap ACL configuration and replay."""

    name: str
    replay: str
    ip_allow: str
    deactivate_ip_allow: bool
    policy: int
    inline: Filter
    named_acls: tuple[tuple[str, Filter], ...] = ()
    expected_responses: tuple[int | None, ...] = ()
    proxy_protocol: bool = False
    generated_replay: bool = False


@dataclass(frozen=True)
class OldActionCase:
    """One obsolete action that must prevent ATS startup."""

    name: str
    acl_filter: Filter
    ip_allow: str
    diagnostic: str


def _yaml_filter(expression: str) -> tuple[str, ...]:
    """Translate a classic @-parameter ACL into YAML filter fields."""

    values: dict[str, list[str]] = {}
    for token in expression.split():
        if not token.startswith("@") or "=" not in token:
            continue
        name, value = token[1:].split("=", 1)
        if name in ("src_ip", "src_ip_category") and value.startswith("~"):
            name += "_invert"
            value = value[1:]
        values.setdefault(name, []).append(value)
    lines = []
    for name, entries in values.items():
        value = f"[{', '.join(entries)}]" if len(entries) > 1 else entries[0]
        lines.append(f"{name}: {value}")
    return tuple(lines)


def _filter(expression: str, use_yaml: bool) -> Filter:
    return _yaml_filter(expression) if use_yaml else expression


def standard_acl_cases(*, use_yaml: bool) -> list[AclCase]:
    """Return the focused ACL cases that precede the combination tables."""

    def case(
            name: str,
            replay: str,
            inline: str,
            expected: tuple[int | None, ...],
            *,
            named: tuple[tuple[str, str], ...] = (),
            deactivate: bool = False,
            proxy_protocol: bool = False,
    ) -> AclCase:
        return AclCase(
            name,
            replay,
            IP_ALLOW_CONTENT,
            deactivate,
            1,
            _filter(inline, use_yaml),
            tuple((acl_name, _filter(definition, use_yaml)) for acl_name, definition in named),
            expected,
            proxy_protocol,
        )

    allow_get_post = "@action=set_allow @src_ip=127.0.0.1 @method=GET @method=POST"
    cases = [
        case("set-allow-methods", "remap_acl_get_post_allowed.replay.yaml", allow_get_post, (200, 200, 403, 403, 403)),
        case(
            "set-allow-methods-proxy-protocol",
            "remap_acl_get_post_allowed_pp.replay.yaml",
            "@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST",
            (200, 200, 403, 403, 403),
            proxy_protocol=True,
        ),
        case(
            "add-one-allowed-method",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=add_allow @src_ip=127.0.0.1 @method=POST",
            (200, 200, 403, 403, 403),
        ),
        case(
            "add-allowed-methods",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=add_allow @src_ip=127.0.0.1 @method=GET @method=POST",
            (200, 200, 403, 403, 403),
        ),
        case(
            "fall-back-to-ip-allow",
            "remap_acl_get_allowed.replay.yaml",
            "@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST",
            (200, 403, 403, 403, 403),
        ),
        case(
            "all-source-addresses",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=set_allow @src_ip=all @method=GET @method=POST",
            (200, 200, 403, 403, 403),
        ),
        case(
            "source-ip-category",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=set_allow @src_ip_category=ACME_LOCAL @method=GET @method=POST",
            (200, 200, 403, 403, 403),
        ),
        case(
            "implicit-all-source-addresses",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=set_allow @method=GET @method=POST",
            (200, 200, 403, 403, 403),
        ),
        case(
            "set-denied-methods",
            "remap_acl_get_post_denied.replay.yaml",
            "@action=set_deny @src_ip=127.0.0.1 @method=GET @method=POST",
            (403, 403, 200, 200, 400),
        ),
        case(
            "add-denied-method",
            "remap_acl_all_denied.replay.yaml",
            "@action=add_deny @src_ip=127.0.0.1 @method=GET",
            (403, 403, 403, 403, 403),
        ),
        case(
            "default-named-deny",
            "remap_acl_all_denied.replay.yaml",
            "@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST",
            (403, 403, 403, 403, 403),
            named=(("deny", "@action=set_deny"),),
        ),
        case(
            "inverted-source-address-no-match",
            "remap_acl_all_denied.replay.yaml",
            "@action=set_allow @src_ip=~127.0.0.1 @method=GET @method=POST",
            (403, 403, 403, 403, 403),
            named=(("deny", "@action=set_deny"),),
        ),
        case(
            "inverted-source-address-match",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=set_allow @src_ip=~3.4.5.6 @method=GET @method=POST",
            (200, 200, 403, 403, 403),
            named=(("deny", "@action=set_deny"),),
        ),
        case(
            "inverted-source-category-no-match",
            "remap_acl_all_denied.replay.yaml",
            "@action=set_allow @src_ip_category=~ACME_LOCAL @method=GET @method=POST",
            (403, 403, 403, 403, 403),
            named=(("deny", "@action=set_deny"),),
        ),
        case(
            "inverted-source-category-match",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=set_allow @src_ip_category=~ACME_EXTERNAL @method=GET @method=POST",
            (200, 200, 403, 403, 403),
            named=(("deny", "@action=set_deny"),),
        ),
        case(
            "source-address-and-category",
            "remap_acl_all_denied.replay.yaml",
            "@action=set_allow @src_ip=127.0.0.1 @src_ip_category=ACME_EXTERNAL @method=GET @method=POST",
            (403, 403, 403, 403, 403),
            named=(("deny", "@action=set_deny"),),
        ),
        case(
            "inline-before-named",
            "remap_acl_get_post_allowed.replay.yaml",
            allow_get_post,
            (200, 200, 403, 403, 403),
            named=(("deny", "@action=set_deny"),),
        ),
        case("inline-overrides-ip-allow", "remap_acl_get_post_allowed.replay.yaml", allow_get_post, (200, 200, 403, 403, 403)),
        case(
            "deactivate-ip-allow",
            "remap_acl_all_allowed.replay.yaml",
            "@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST",
            (200, 200, 200, 200, 400),
            deactivate=True,
        ),
        case(
            "inbound-ip-match",
            "remap_acl_get_post_allowed.replay.yaml",
            "@action=set_allow @in_ip=127.0.0.1 @method=GET @method=POST",
            (200, 200, 403, 403, 403),
        ),
        case(
            "inbound-ip-no-match",
            "remap_acl_get_allowed.replay.yaml",
            "@action=set_allow @in_ip=3.4.5.6 @method=GET @method=POST",
            (200, 403, 403, 403, 403),
        ),
        case(
            "named-deny-without-inline",
            "deny_head_post.replay.yaml",
            "",
            (200, 403, 403, 403),
            named=(("deny", "@action=set_deny @method=HEAD @method=POST"),),
        ),
    ]
    return cases


def combination_acl_cases(records: Sequence[Mapping[str, object]], *, prefix: str) -> list[AclCase]:
    """Convert one existing ACL expectation table into independent cases."""

    cases = []
    for record in records:
        inline = _normalize_filter(record["inline"])
        named = _normalize_filter(record["named_acl"])
        cases.append(
            AclCase(
                f"{prefix}-{record['index']}",
                "base.replay.yaml",
                str(record["ip_allow"]),
                bool(record.get("deactivate_ip_allow", False)),
                0 if record["policy"] == "legacy" else 1,
                inline,
                (("acl", named),) if named else (),
                (record["GET response"], record["POST response"]),  # type: ignore[arg-type]
                generated_replay=True,
            ))
    return cases


def old_action_cases(*, use_yaml: bool) -> list[OldActionCase]:
    """Return obsolete action configurations rejected by modern policy."""

    return [
        OldActionCase(
            "inline-allow",
            _filter("@action=allow @method=GET", use_yaml),
            IP_ALLOW_CONTENT,
            '"allow" and "deny" are no longer valid.',
        ),
        OldActionCase(
            "inline-deny",
            _filter("@action=deny @method=GET", use_yaml),
            IP_ALLOW_CONTENT,
            '"allow" and "deny" are no longer valid.',
        ),
        OldActionCase("ip-allow-action", () if use_yaml else "", IP_ALLOW_OLD_ACTION, "Legacy action name of"),
    ]


def _normalize_filter(value: object) -> Filter:
    if isinstance(value, str):
        return value
    if isinstance(value, Sequence):
        return tuple(str(entry) for entry in value)
    raise TypeError(f"Unsupported ACL filter: {value!r}")


class RemapAclScenario:
    """Run one remap ACL case with independently owned processes."""

    def __init__(
        self,
        ats_factory: ATSFactory,
        services: ServiceFactory,
        case: AclCase,
        *,
        use_yaml: bool,
        test_directory: Path,
    ) -> None:
        self._ats_factory = ats_factory
        self._services = services
        self._case = case
        self._use_yaml = use_yaml
        self._test_directory = test_directory
        self._replay = self.configure_replay()
        self._origin = self.configure_origin()
        self._ats = self.configure_ats()

    def configure_replay(self) -> Path:
        """Render the expected statuses for a table-driven replay."""

        source = self._test_directory / self._case.replay
        if not self._case.generated_replay:
            return source
        document = yaml.safe_load(source.read_text())
        responses = iter(self._case.expected_responses)
        for session in document["sessions"]:
            for transaction in session["transactions"]:
                expected = next(responses)
                transaction["proxy-response"]["status"] = 403 if expected is None else expected
        destination = self._ats_factory.run_directory / f"{self._case.name}.replay.yaml"
        destination.write_text(yaml.safe_dump(document, sort_keys=False))
        return destination

    def configure_origin(self) -> VerifierServer:
        """Create the Proxy Verifier origin for this case."""

        return self._services.verifier_server("origin", self._replay)

    def _classic_remap(self) -> list[str]:
        lines = []
        if self._case.deactivate_ip_allow:
            lines.append(".deactivatefilter ip_allow")
        for name, definition in self._case.named_acls:
            lines.append(f".definefilter {name} {definition}")
            lines.append(f".activatefilter {name}")
        lines.append(f"map / http://127.0.0.1:{self._origin.http_port} {self._case.inline}")
        return lines

    def _yaml_remap(self) -> list[str]:
        lines = ["remap:"]
        if self._case.deactivate_ip_allow:
            lines.append("  - deactivate_filter: ip_allow")
        for name, definition in self._case.named_acls:
            values = tuple(definition) if not isinstance(definition, str) else _yaml_filter(definition)
            if not values:
                continue
            lines.extend(["  - define_filter:", f"      {name}:"])
            lines.extend(f"        {value}" for value in values)
            lines.append(f"  - activate_filter: {name}")
        lines.extend([
            "  - type: map",
            "    from: {url: '/'}",
            f"    to: {{url: 'http://127.0.0.1:{self._origin.http_port}'}}",
        ])
        inline = tuple(self._case.inline) if not isinstance(self._case.inline, str) else _yaml_filter(self._case.inline)
        if inline:
            lines.append("    acl_filter:")
            lines.extend(f"      {value}" for value in inline)
        return lines

    def configure_ats(self) -> ATS:
        """Configure ATS with this case's policy and filters."""

        ats = self._ats_factory.create("ts", enable_cache=False, enable_proxy_protocol=True)
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|url|remap|ip_allow|proxyprotocol",
                "proxy.config.http.push_method_enabled": 1,
                "proxy.config.http.connect_ports": self._origin.http_port,
                "proxy.config.url_remap.acl_behavior_policy": self._case.policy,
                "proxy.config.acl.subjects": "PROXY,PEER",
            })
        ats.ip_allow_config.add_lines(self._case.ip_allow)
        (ats.remap_yaml
         if self._use_yaml else ats.remap_config).add_lines(self._yaml_remap() if self._use_yaml else self._classic_remap())
        return ats

    def configure_client(self) -> ProcessService:
        """Create a verifier client with rejection-aware expectations."""

        was_rejected_at_accept = self._case.expected_responses == (None, None)
        port = self._ats.proxy_protocol_port if self._case.proxy_protocol else self._ats.http_port
        return self._services.verifier_client(
            "client",
            self._replay,
            http_ports=[port],
            return_code=1 if was_rejected_at_accept else 0,
            allow_errors=was_rejected_at_accept,
        )

    def run(self) -> None:
        """Run the configured ACL request sequence."""

        self._origin.start()
        self._ats.start()
        result = self.configure_client().run()
        if self._case.expected_responses == (None, None):
            assert result.returncode == 1, result.output
            diagnostic = self._ats.diags_log.read_text(errors="replace")
            assert "client '127.0.0.1' prohibited by ip-allow policy" in diagnostic


class OldAclActionScenario:
    """Verify obsolete ACL actions fail ATS startup under modern policy."""

    def __init__(self, ats_factory: ATSFactory, case: OldActionCase, *, use_yaml: bool) -> None:
        self._case = case
        self._use_yaml = use_yaml
        self._ats = self.configure_ats(ats_factory)

    def configure_ats(self, ats_factory: ATSFactory) -> ATS:
        """Configure one obsolete remap or ip_allow action."""

        ats = ats_factory.create("ts")
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|url|remap|ip_allow",
                "proxy.config.url_remap.acl_behavior_policy": 1,
            })
        ats.ip_allow_config.add_lines(self._case.ip_allow)
        if self._use_yaml:
            lines = ["remap:", "  - type: map", "    from: {url: '/'}", "    to: {url: 'http://127.0.0.1:8080'}"]
            acl_filter = tuple(self._case.acl_filter)
            if acl_filter:
                lines.append("    acl_filter:")
                lines.extend(f"      {value}" for value in acl_filter)
            ats.remap_yaml.add_lines(lines)
        else:
            ats.remap_config.add_line(f"map / http://127.0.0.1:8080 {self._case.acl_filter}")
        ats.expect_start_failure(self._case.diagnostic, (33, 70))
        return ats

    def run(self) -> None:
        """Start ATS and observe the expected fatal validation error."""

        self._ats.start()
