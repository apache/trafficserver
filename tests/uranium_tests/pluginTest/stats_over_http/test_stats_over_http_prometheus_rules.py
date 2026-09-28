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
"""Serve Prometheus stats named by a rules file, and load the file again after a change and a reload."""
import json
import os
import re
import sys
from pathlib import Path
import pytest
from tools.uranium.services import ATS, ATSFactory, ServiceFactory

RULES = Path(__file__).parent / "rules"
TEST_METRICS = "plugin.test_metrics.requests.get=7 plugin.test_metrics.requests.post=9"
LABELED = ['plugin_test_metrics_requests{method="get"} 7', 'plugin_test_metrics_requests{method="post"} 9']


def create_ats(ats_factory: ATSFactory, name: str) -> ATS:
    """Create an ATS with the test metrics and the rules files.

    :param ats_factory: Proxy factory.
    :param name: The name of the ATS instance.
    """
    ats = ats_factory.create(name)
    if not ats.plugin_exists("stats_over_http.so"):
        pytest.skip("stats_over_http.so is required")
    ats.copy_custom_plugin("{ATS_TEST_PLUGINS_DIR}/test_metrics.so")
    ats.plugin_config.add_line(f"test_metrics.so {TEST_METRICS}")
    ats.records.update({
        "proxy.config.diags.debug.enabled": 1,
        "proxy.config.diags.debug.tags": "stats_over_http",
    })
    ats.write_config_file("rules.yaml", (RULES / "labels.yaml").read_text())
    return ats


def install(ats: ATS, rules: str, target: str = "rules.yaml") -> None:
    """Copy a file of the rules directory over a rules file of a running ATS.

    :param ats: The ATS whose configuration directory has the rules file.
    :param rules: The file of the rules directory.
    :param target: The rules file to replace.
    """
    (ats.config_directory / target).write_text((RULES / rules).read_text())


def scrape(
    services: ServiceFactory,
    ats: ATS,
    name: str,
    expect: list[str],
    status: int = 200,
    wait: bool = False,
    path: str = "/metrics",
    options: tuple[str, ...] = ()) -> None:
    """Scrape a path and check the response.

    :param services: Scrape client factory.
    :param ats: The ATS to scrape.
    :param name: The client process name.
    :param expect: Lines that the body should have.
    :param status: The expected response status.
    :param wait: Whether to fetch again until the status and every expected line are there.
    :param path: The path to scrape.
    :param options: More check_prometheus.py options.
    """
    arguments = [
        sys.executable,
        Path(__file__).parent / "check_prometheus.py",
        str(ats.http_port), path, f"--status={status}", *options
    ]
    arguments += ["--wait"] if wait else []
    arguments += [f"--expect={line}" for line in expect]
    result = services.process(name, arguments).run()
    assert result.returncode == 0, result.output


def task_names(info: dict) -> set[str]:
    """The descriptions and file names of a reload task and its subtasks.

    :param info: A task of a get_reload_config_status response.
    """
    names = set()
    for field in ("description", "filename"):
        value = info.get(field)
        if value and value != "<none>":
            names.add(os.path.basename(value))
    for sub in info.get("sub_tasks", []):
        names |= task_names(sub)
    return names


def reload(ats: ATS, token: str, succeeds: bool = True, task: str | None = None) -> None:
    """Reload the configuration, wait until the reload ends, and check its result.

    :param ats: The ATS to reload.
    :param token: The reload token.
    :param succeeds: Whether the reload should succeed rather than fail.
    :param task: A task that the reload should have, if any.
    """
    result = ats.traffic_ctl("config", "reload", "-m", "-t", token, "-w", "1", "-r", "0.5", "-T", "30s")
    assert result.returncode == (0 if succeeds else 2), result.output
    if task is None:
        return
    status = ats.rpc({"jsonrpc": "2.0", "id": token, "method": "get_reload_config_status", "params": {"token": token}})
    assert status.returncode == 0, status.output
    tasks = json.loads(status.stdout).get("result", {}).get("tasks", [])
    assert tasks, f"The reload {token} has no tasks."
    assert task in task_names(tasks[0]), f"The reload {token} should have the task {task}: {sorted(task_names(tasks[0]))}"


def test_stats_over_http_prometheus_rules_reload(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """Verify the rules files of remap rules and their reloads.

    :param ats_factory: Proxy factory.
    :param services: Scrape client factory.
    """
    ats = create_ats(ats_factory, "ts_reload")
    ats.write_config_file("labels.yaml", (RULES / "labels.yaml").read_text())
    ats.write_config_file("settings.yaml", (RULES / "settings.yaml").read_text())
    ats.remap_config.add_lines(
        [
            f"map http://127.0.0.1:{ats.http_port}/metrics http://127.0.0.1/"
            " @plugin=stats_over_http.so @pparam=--config=rules.yaml @pparam=--on-config-error=503",
            f"map http://127.0.0.1:{ats.http_port}/labels http://127.0.0.1/"
            " @plugin=stats_over_http.so @pparam=--config=labels.yaml",
            f"map http://127.0.0.1:{ats.http_port}/settings http://127.0.0.1/"
            " @plugin=stats_over_http.so @pparam=--config=settings.yaml",
        ])
    ats.start()

    # The rules label a family.  The settings file sets the format and max_age_ms 0, so each request gets a new render.
    scrape(services, ats, "rules", LABELED)
    scrape(services, ats, "settings", [], path="/settings", options=("--fresh", "--help-lines"))

    # The rules stay until a reload, which loads the changed rules.
    install(ats, "labels_verb.yaml")
    scrape(services, ats, "before-reload", ['plugin_test_metrics_requests{method="get"} 7'])
    reload(ats, "changed", task="remap.config")
    scrape(services, ats, "after-reload", ['plugin_test_metrics_requests{verb="get"} 7'], wait=True)

    # After a reload with a broken rules file, a rule with --on-config-error=503 answers with a 503.  A reload after a fix
    # loads the rules again.
    install(ats, "no_capture_group.yaml")
    reload(ats, "broken", task="remap.config")
    scrape(services, ats, "broken", [], status=503, wait=True)
    install(ats, "labels.yaml")
    reload(ats, "fixed", task="remap.config")
    scrape(services, ats, "fixed", ['plugin_test_metrics_requests{method="get"} 7'], wait=True)

    # Without --on-config-error, a broken rules file fails the reload, and the old rules stay.
    install(ats, "no_capture_group.yaml", target="labels.yaml")
    reload(ats, "failed", succeeds=False, task="remap.config")
    scrape(services, ats, "failed", LABELED, path="/labels")

    ats.stop()
    diags = ats.diags_log.read_text(errors="replace")
    assert re.search(r"rules\.yaml: line 20: prometheus\.rules\.match: .* has no capture group", diags), \
        "The plugin should log the error in the rules file."
    assert re.search(
        r"Writing plugin\.test_metrics\.requests\.post with the labels \(method\) of plugin_test_metrics_requests instead"
        r" of its own labels \(verb\)", diags), "The plugin should log the relabeled sample."
    assert re.search(r"labels\.yaml: line 20: prometheus\.rules\.match: .* has no capture group", diags), \
        "The failed reload should log the error in the rules file."
    assert "FATAL:" not in diags, diags
    assert "Unrecognized configuration value" not in diags, diags


def test_stats_over_http_prometheus_rules_reload_yaml(ats_factory: ATSFactory, services: ServiceFactory) -> None:
    """The same reload when Traffic Server loads remap.yaml instead of remap.config.

    :param ats_factory: Proxy factory.
    :param services: Scrape client factory.
    """
    ats = create_ats(ats_factory, "ts_reload_yaml")
    ats.remap_yaml.add_lines(
        [
            "remap:",
            "  - type: map",
            "    from:",
            f"      url: http://127.0.0.1:{ats.http_port}/metrics",
            "    to:",
            "      url: http://127.0.0.1/",
            "    plugins:",
            "      - name: stats_over_http.so",
            "        params:",
            "          - --config=rules.yaml",
            "          - --on-config-error=503",
        ])
    ats.start()

    scrape(services, ats, "rules", LABELED)
    install(ats, "labels_verb.yaml")
    reload(ats, "changed")
    scrape(services, ats, "after-reload", ['plugin_test_metrics_requests{verb="get"} 7'], wait=True)

    ats.stop()
    assert re.search(r"remap\.yaml finished loading", ats.diags_log.read_text(errors="replace")), \
        "Traffic Server should load remap.yaml."
