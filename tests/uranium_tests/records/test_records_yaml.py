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

import json

from tools.uranium.services import ATS, ATSFactory
from uranium_tests.lib.jsonrpc import Request


def configure_unregistered_records(*, _ats_factory: ATSFactory) -> ATS:
    """Configure unknown records, null strings, and a size multiplier.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = _ats_factory.create("unregistered", disable_log_checks=True)
    ats.write_config_file(
        "records.yaml", """records:
  config_update_interval_ms: 20
  http:
    server_ports: '{ATS_HTTP_PORT}'
    wait_for_cache: 1
  diags:
    debug:
      enabled: 1
      tags: rpc|rec
  ssl:
    client:
      cert:
        filename: null
        filenamee: some.txt
        filenam: some2.txt
  dns:
    nameservers: null
  test:
    not_registered:
      field1: !!int 1
      field2: 0
  cache:
    ram_cache:
      size: 30G
""")
    return ats


def verify_unregistered_records(*, _ats_factory: ATSFactory) -> None:
    """Verify unknown records remain queryable but are marked unregistered.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = configure_unregistered_records(_ats_factory=_ats_factory)
    ats.start()

    diags = ats.diags_log.read_text(errors="replace")
    record = "proxy.config.test.not_registered.field1"
    assert f"Unrecognized configuration value '{record}'" in diags

    output = ats.traffic_out.read_text(errors="replace")
    assert "Ignoring field 'filenamee' [proxy.config.ssl.client.cert.filenamee]" in output
    assert "Ignoring field 'filenam' [proxy.config.ssl.client.cert.filenam]" in output
    assert "Ignoring field 'field2' [proxy.config.test.not_registered.field2]" in output

    result = ats.rpc(
        Request.admin_lookup_records([{
            "record_name_regex": "proxy.config.test.not_registered.field",
            "rec_types": ["1", "16"],
        }]))
    assert result.returncode == 0, result.output
    response = json.loads(result.stdout)
    records = response["result"]["recordList"]
    assert len(records) == 1
    assert all(entry["record"]["registered"] == "false" for entry in records)

    result = ats.traffic_ctl("config", "get", "proxy.config.cache.ram_cache.size")
    assert result.returncode == 0, result.output
    assert "proxy.config.cache.ram_cache.size: 32212254720" in result.stdout


def verify_multiple_documents(*, _ats_factory: ATSFactory) -> None:
    """Verify later documents override values and preserve YAML nulls.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = _ats_factory.create("documents")
    ats.append_records_document({
        "proxy.config.diags.debug.enabled": 0,
        "proxy.config.diags.debug.tags": "rpc|rec",
    })
    ats.append_records_document({"proxy.config.diags.debug.tags": "filemanager"})
    ats.append_records_document(
        {
            "proxy.config.dns.resolv_conf": None,
            "proxy.config.dns.nameservers": None,
            "proxy.config.dns.local_ipv6": None,
            "proxy.config.ssl.client.cert.filename": None,
        })
    ats.start()

    result = ats.traffic_ctl(
        "config",
        "get",
        "proxy.config.diags.debug.enabled",
        "proxy.config.diags.debug.tags",
        "proxy.config.dns.resolv_conf",
        "proxy.config.dns.local_ipv6",
        "proxy.config.dns.nameservers",
        "proxy.config.ssl.client.cert.filename",
    )
    assert result.returncode == 0, result.output
    assert "proxy.config.diags.debug.enabled: 0" in result.stdout
    assert "proxy.config.diags.debug.tags: filemanager" in result.stdout
    for record in (
            "proxy.config.dns.resolv_conf",
            "proxy.config.dns.local_ipv6",
            "proxy.config.dns.nameservers",
            "proxy.config.ssl.client.cert.filename",
    ):
        assert f"{record}: null" in result.stdout


def verify_invalid_document_does_not_stop_parsing(*, _ats_factory: ATSFactory) -> None:
    """Verify an invalid value does not prevent parsing the next document.

    :param _ats_factory: Test-local ats factory configured by the test.
    """

    ats = _ats_factory.create("invalid-document")
    ats.records.update({"proxy.config.some_invalid_field_should_not_block_further_docs_from_the_parser_logic": "OK"})
    ats.append_records_document({"proxy.config.diags.debug.tags": "rpc|rec"})
    ats.start()

    result = ats.traffic_ctl("config", "get", "proxy.config.diags.debug.tags")
    assert result.returncode == 0, result.output
    assert "proxy.config.diags.debug.tags: rpc|rec" in result.stdout


def test_records_yaml(ats_factory: ATSFactory) -> None:
    """records.yaml handles unknown values and multiple YAML documents.

    :param ats_factory: Factory for isolated Traffic Server instances.
    """

    verify_unregistered_records(_ats_factory=ats_factory)
    verify_multiple_documents(_ats_factory=ats_factory)
    verify_invalid_document_does_not_stop_parsing(_ats_factory=ats_factory)
