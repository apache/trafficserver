# Uranium Pytest Framework

This directory implements the pytest framework used by Traffic Server's
Uranium system tests. Test cases live in `tests/uranium_tests`; this directory
contains collection, configuration, execution, process, and fixture support.

See the [test-suite README](../../README.md) for commands and the
[Uranium developer guide](../../../doc/developer-guide/testing/uranium-tests.en.rst)
for test-authoring guidance.

## Mental model

The framework has five layers:

| Layer | Main module | Responsibility |
|---|---|---|
| Command entry point | [`runner.py`](runner.py) | Select the host or container environment and invoke pytest. |
| Pytest integration | [`plugin.py`](plugin.py) | Collect replay files, create pytest items, apply markers, and provide fixtures. |
| Test specification | [`config.py`](config.py) | Parse and validate replay metadata into `ReplaySpec` objects. |
| Test execution | [`replay.py`](replay.py) | Materialize a sandbox, run the declared processes, and validate results. |
| Runtime primitives | [`runtime.py`](runtime.py), [`process.py`](process.py), [`expectations.py`](expectations.py), and [`services/`](services/) | Discover programs, own processes, express expected output, and provide procedural-test APIs. |

The short distinction between the three replay-facing modules is:

- `plugin.py` adapts replay files to pytest.
- `config.py` determines what a replay test means.
- `replay.py` makes that test happen.

## Direct replay flow

A direct replay test is a `*.test.yaml` or `*.test.yml` file. One file can
serve two related purposes:

- The `urtest` mapping describes test orchestration, ATS configuration,
  requirements, and expected results.
- The remaining YAML can contain Proxy Verifier traffic. Alternatively,
  `urtest.replay` can name a separate traffic file.

The execution path is:

1. [`runner.py`](runner.py) prepares the selected environment and starts
   pytest with [`plugin.py`](plugin.py) loaded.
2. `plugin.py::pytest_collect_file()` recognizes the replay suffix and creates
   a `ReplayFile` collector.
3. `ReplayFile.collect()` calls `config.py::ReplaySpec.load_all()`. The loader
   reads YAML, validates the `urtest` mapping, resolves the traffic file, and
   merges any variants.
4. The collector creates one `ReplayItem` per `ReplaySpec`, including one item
   per variant, and applies replay and manual markers.
5. `ReplayItem.runtest()` obtains a [`TestRuntime`](runtime.py) and passes the
   validated specification to `replay.py::ReplayTest`.
6. `ReplayTest.run()` creates the sandbox, starts DNS and Proxy Verifier as
   requested, materializes and starts ATS, runs the client, validates files,
   metrics, logs, and process output, and finally stops every process.

`config.py` also contains small ATS-configuration serialization helpers used
by `replay.py`, such as flat-record conversion and YAML writing. Those helpers
operate on data; process orchestration remains in `replay.py`.

## Procedural test flow

Tests that require custom clients, servers, runtime mutations, or multiple ATS
instances are ordinary `test_*.py` modules:

1. Pytest collects the module normally.
2. [`plugin.py`](plugin.py) supplies fixtures such as `ats`, `ats_factory`,
   `curl`, `procedural_context`, and `services`.
3. [`services/context.py`](services/context.py) identifies the test's sandbox
   and source directory.
4. [`services/ats.py`](services/ats.py) owns ATS instances, while
   [`services/service_factory.py`](services/service_factory.py) owns origins,
   DNS servers, Proxy Verifier processes, and other support programs.
5. All subprocesses ultimately use [`process.py`](process.py) for lifecycle and
   output capture, and [`runtime.py`](runtime.py) for installed paths, features,
   ports, and sandboxes.

The procedural `ATS` service reuses ATS setup and startup functionality from
`ReplayTest`. This keeps direct replay and procedural ATS behavior consistent,
although it means `replay.py` is also an implementation dependency of
procedural tests.

## Top-level modules

### `assertions.py`

Defines assertions shared by direct replay and procedural tests. Gold-file
matching lives here, including the optional `cdifflib` acceleration and the
standard-library fallback.

### `config.py`

Defines `ReplaySpec` and `ReplayConfigError`. It loads replay manifests,
validates required metadata, expands variants, resolves replay paths, merges
flat ATS records, formats plugin entries, substitutes ports, and writes YAML.
It does not start processes.

### `expectations.py`

Defines the explicit process-stream expectation API shared by managed
procedural services. It accumulates ``contains()``, ``excludes()``, and
``matches_gold()`` declarations, supports intentional ``reset()``, and rejects
the legacy ``+=`` registration form. ``process.py`` owns each stream object and
``ProcessService`` validates it after a process completes or is stopped.

### `plugin.py`

Defines pytest hooks, custom replay collectors and items, markers, sharding,
serial scheduling, manual-test selection, and the public Uranium fixtures. It
connects pytest to `ReplaySpec`, `ReplayTest`, and the procedural services.

### `process.py`

Defines the low-level `ManagedProcess` primitive and `ProcessError`.
`ManagedProcess` owns subprocess startup, readiness polling, signals, captured
stdout and stderr, explicit stream and return-code expectations, timeouts, and
cleanup.

### `replay.py`

Defines the direct-replay execution engine. It prepares ATS configuration and
run-root files, launches the declared server, ATS, DNS, and client processes,
runs lifecycle commands, replaces runtime placeholders, and validates expected
outputs.

### `runner.py`

Implements the Python entry point used by `tests/urtest.sh` and generated
build-tree wrappers. It separates wrapper options from pytest arguments,
selects Docker or direct execution, prepares the container build, handles CI
shards, chooses a short sandbox path, and restores artifact ownership.

### `runtime.py`

Defines `TestRuntime`, the immutable description of one installed test
environment. It discovers ATS and helper programs, reads build features and
layout, allocates ports, creates worker-specific sandboxes, resolves generated
artifacts, and coordinates execution that must be exclusive.

### `utils.py`

Defines small address, TCP-readiness, and version helpers shared by the replay
engine and procedural services.

## `services/` package

[`services/__init__.py`](services/__init__.py) is the stable public facade used
by procedural tests:

```python
from tools.uranium.services import ATS, Curl, ServiceFactory
```

The implementations are divided by responsibility:

| Module | Responsibility |
|---|---|
| [`ats.py`](services/ats.py) | ATS configuration, lifecycle, tools, logs, signals, and `ATSFactory`. |
| [`context.py`](services/context.py) | `ProceduralContext` sandbox state and `CommandResult` values. |
| [`curl.py`](services/curl.py) | Curl execution and ATS-aware TCP or Unix-domain-socket transport. |
| [`process_service.py`](services/process_service.py) | Procedural wrapper around `ManagedProcess`, adding readiness and output validation. |
| [`origin.py`](services/origin.py) | Microserver lifecycle and programmatic request/response sessions. |
| [`dns.py`](services/dns.py) | MicroDNS lifecycle, readiness, and record updates. |
| [`httpbin.py`](services/httpbin.py) | `go-httpbin` lifecycle and readiness. |
| [`verifier.py`](services/verifier.py) | Proxy Verifier server lifecycle and violation checks. |
| [`service_factory.py`](services/service_factory.py) | Creation and cleanup of non-ATS services and arbitrary support processes. |
| [`service_utils.py`](services/service_utils.py) | File polling, TCP requests, and metric polling. |

The distinction between the two process layers is intentional:

- `process.py::ManagedProcess` is the general subprocess primitive.
- `services/process_service.py::ProcessService` adds behavior expected by
  procedural pytest scenarios.

## Supporting directories

### `min_cfg/`

Contains the smallest baseline configuration copied into each ATS sandbox:

- `ip_allow.yaml` provides safe default inbound and outbound access rules.
- `storage.yaml` provides a minimal cache storage span.
- `readme.txt` records why these baseline files are still necessary.

### `tests/`

Contains fast framework tests rather than Traffic Server scenarios:

- `test_config.py` covers metadata parsing, variants, replay inventory, and
  configuration transformations.
- `test_assertions.py` covers shared gold-file comparison and diff selection.
- `test_plugin.py` covers pytest marker and manual-test behavior.
- `test_process.py` covers subprocess handling and placeholder replacement.
- `test_runner.py` covers wrapper arguments, containers, sharding, builds, and
  sandbox copying.
- `test_services.py` covers the public service facade, ATS ownership, cleanup,
  Curl argument parsing, and Unix-domain-socket behavior.

## Where new code belongs

- Put replay metadata parsing or validation in `config.py`.
- Put pytest collection, selection, markers, or fixtures in `plugin.py`.
- Put direct replay lifecycle and validation behavior in `replay.py`.
- Put environment discovery, ports, or sandbox allocation in `runtime.py`.
- Put generic subprocess behavior in `process.py`.
- Put procedural-test APIs in the appropriate `services/` module.
- Put framework regression tests in `tests/`.
