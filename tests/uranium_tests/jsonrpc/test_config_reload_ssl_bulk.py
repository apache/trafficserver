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
"""Exercise rich status output for a bulk SSL certificate reload."""

from tools.uranium.services import ATS, ATSFactory, wait_for_file_lines

from .config_reload_helpers import require_command, wait_for_status

NUM_CERTS = 20
BAD_CERTS = (5, 9, 13, 17)


class ConfigReloadSslBulkScenario:
    """Load 20 certificates, introduce failures, and recover."""

    def __init__(self, ats_factory: ATSFactory) -> None:
        """Configure the single-certificate baseline.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Create a TLS-enabled instance with valid baseline config.

        :param ats_factory: Factory for isolated Traffic Server instances.
        """

        ats = ats_factory.create("ts", enable_tls=True)
        ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.ssl.server.cert.path": str(ats.ssl_directory),
                "proxy.config.ssl.server.private_key.path": str(ats.ssl_directory),
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "config.reload",
            })
        ats.ssl_multicert_config.add_lines(
            (
                "ssl_multicert:",
                '  - dest_ip: "*"',
                "    ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
            ))
        ats.write_config_file("sni.yaml", 'sni:\n- fqdn: "*.example.com"\n  verify_client: NONE\n')
        ats.parent_config.add_line("# empty")
        ats.cache_config.add_line("# empty")
        return ats

    def install_bulk_config(self) -> None:
        """Copy the default key pair 20 times and write all entries."""

        ssl_dir = self._ats.ssl_directory
        commands = []
        lines = ["ssl_multicert:"]
        for index in range(1, NUM_CERTS + 1):
            commands.append(
                f'cp "{ssl_dir / "server.pem"}" "{ssl_dir / f"cert-{index:02d}.pem"}" '
                f'&& cp "{ssl_dir / "server.key"}" "{ssl_dir / f"cert-{index:02d}.key"}"')
            lines.extend((f"  - ssl_cert_name: cert-{index:02d}.pem", f"    ssl_key_name: cert-{index:02d}.key"))
        multicert = self._ats.config_directory / "ssl_multicert.yaml"
        document = "\n".join(lines) + "\n"
        script = " && ".join(commands)
        script += f" && printf %b {document!r} > \"{multicert}\""
        script += f' && touch "{self._ats.config_directory / "sni.yaml"}" && traffic_ctl config reload -t ssl-bulk-ok'
        require_command(self._ats.run_shell(script))

    def break_certificates(self) -> None:
        """Corrupt, empty, mismatch, and remove four certificate inputs."""

        ssl_dir = self._ats.ssl_directory
        multicert = self._ats.config_directory / "ssl_multicert.yaml"
        sni = self._ats.config_directory / "sni.yaml"
        command = (
            f'printf "GARBAGE_NOT_A_CERT\\n" > "{ssl_dir / "cert-05.pem"}" '
            f'&& truncate -s 0 "{ssl_dir / "cert-09.pem"}" '
            f'&& cp "{ssl_dir / "cert-01.key"}" "{ssl_dir / "cert-13.key"}" '
            f'&& touch "{ssl_dir / "cert-13.pem"}" '
            f'&& rm -f "{ssl_dir / "cert-17.pem"}" '
            f'&& touch "{multicert}" "{sni}" && traffic_ctl config reload -t ssl-bulk-partial')
        require_command(self._ats.run_shell(command))

    def recover_certificates(self) -> None:
        """Restore every damaged key pair and trigger a recovery reload."""

        ssl_dir = self._ats.ssl_directory
        commands = [
            f'cp "{ssl_dir / "server.pem"}" "{ssl_dir / f"cert-{index:02d}.pem"}" '
            f'&& cp "{ssl_dir / "server.key"}" "{ssl_dir / f"cert-{index:02d}.key"}"' for index in BAD_CERTS
        ]
        commands.append(
            f'touch "{self._ats.config_directory / "ssl_multicert.yaml"}" '
            f'"{self._ats.config_directory / "sni.yaml"}"')
        commands.append("traffic_ctl config reload -t ssl-bulk-recover")
        require_command(self._ats.run_shell(" && ".join(commands)))

    def run(self) -> None:
        """Validate all-success, partial-failure, filtering, and recovery."""

        self._ats.start()
        self.install_bulk_config()
        wait_for_status(
            self._ats,
            "ssl-bulk-ok",
            "ssl_client_coordinator",
            "SSLCertificateConfig",
            "ssl_multicert.yaml finished loading",
            excludes=("FAIL",),
        )

        self.break_certificates()
        wait_for_status(
            self._ats,
            "ssl-bulk-partial",
            "FAIL",
            "[Err]",
            "cert-05",
            "ssl_multicert.yaml failed to load",
        )
        require_command(
            self._ats.traffic_ctl("config", "status", "-t", "ssl-bulk-partial", "--min-level", "warning"),
            "[Err]",
            excludes=("[Note]",),
        )

        self.recover_certificates()
        wait_for_status(
            self._ats,
            "ssl-bulk-recover",
            "ssl_multicert.yaml finished loading",
            excludes=("FAIL",),
        )
        diagnostics = wait_for_file_lines(self._ats.diags_log, r"Config reload \[ssl-bulk-recover\]", 1)
        assert "Config reload [ssl-bulk-ok] completed" in diagnostics
        assert "Config reload [ssl-bulk-partial] finished with failures" in diagnostics
        assert "Config reload [ssl-bulk-recover] completed" in diagnostics
        assert "ignoring transition from" not in diagnostics


def test_config_reload_ssl_bulk(ats_factory: ATSFactory) -> None:
    """Bulk certificate status reports partial failure and recovery.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    ConfigReloadSslBulkScenario(ats_factory).run()
