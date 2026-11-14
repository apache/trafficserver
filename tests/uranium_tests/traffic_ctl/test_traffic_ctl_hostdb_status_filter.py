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
"""HostDB hostname filters retain every existing partition and its records array."""

import json
from pathlib import Path
import pytest
from tools.uranium.services import ATSFactory, ServiceFactory


def partition_names(partitions: object) -> dict[str, list[str]]:
    """Validate partition structure before extracting record names.

    :param partitions: Decoded partitions array from CLI or RPC output.
    """
    assert isinstance(partitions, list) and partitions, partitions
    names = {}
    for partition in partitions:
        assert isinstance(partition, dict) and "id" in partition, partition
        assert isinstance(partition.get("records"), list), partition
        assert partition["id"] not in names, partition
        names[partition["id"]] = sorted(record["metadata"]["name"] for record in partition["records"])
    return names


@pytest.mark.parametrize("partitions", [None, 1], ids=["default", "single"])
def test_traffic_ctl_hostdb_status_filter(ats_factory: ATSFactory, services: ServiceFactory, partitions: int | None) -> None:
    """Populate two names and compare exact/substring/no-match RPC and CLI filters.

    :param ats_factory: Proxy factory.
    :param services: DNS and verifier factory.
    :param partitions: Explicit partition count, or None for the default.
    """
    replay = Path(__file__).parent / "replay/hostdb_status_filter.replay.yaml"
    origin = services.verifier_server("origin", replay)
    dns = services.dns("dns", default="127.0.0.1")
    ats = ats_factory.create("ts", enable_cache=False)
    ats.records.update({"proxy.config.dns.nameservers": f"127.0.0.1:{dns.port}", "proxy.config.dns.resolv_conf": "NULL"})
    if partitions is not None:
        ats.records.update({"proxy.config.hostdb.partitions": partitions})
    for hostname in ("one.hostdb.test", "two.hostdb.test"):
        ats.remap_config.add_line(f"map http://{hostname}/ http://{hostname}:{origin.http_port}/")
    dns.start()
    origin.start()
    ats.start()
    result = services.verifier_client("client", replay, http_ports=[ats.http_port]).run()
    assert result.returncode == 0, result.output
    baseline = {}
    for hostname in ("", "one.hostdb.test", "ne.hostdb", "no.such.host"):
        result = ats.rpc({"jsonrpc": "2.0", "id": "status", "method": "get_hostdb_status", "params": {"hostname": hostname}})
        assert result.returncode == 0, result.output
        document = json.loads(result.stdout)
        assert "error" not in document, document
        names = partition_names(document["result"]["data"]["partitions"])
        if not hostname:
            baseline = names
            assert all(names.values()), names
            assert {name for records in names.values() for name in records} == {"one.hostdb.test", "two.hostdb.test"}
            if partitions == 1:
                assert list(names) == ["0"], names
        else:
            expected = {pid: [name for name in records if hostname in name] for pid, records in baseline.items()}
            assert names == expected
            result = ats.traffic_ctl("hostdb", "status", hostname)
            assert result.returncode == 0, result.output
            assert partition_names(json.loads(result.stdout)["partitions"]) == expected
