# Uranium Pytest Framework

This directory implements the pytest framework used by Traffic Server's
Uranium system tests. Test cases live in `tests/uranium_tests`; this directory
contains collection, configuration, execution, process, and fixture support.

See the [test-suite README](../../README.md) for commands and the
[Uranium developer guide](../../../doc/developer-guide/testing/uranium-tests.en.rst)
for test-authoring guidance.

## Mental model

The framework has six layers:

| Layer | Main module | Responsibility |
|---|---|---|
| Command entry point | [`runner.py`](runner.py) | Select the host or container environment and invoke pytest. |
| Pytest integration | [`plugin.py`](plugin.py) | Collect replay files, create pytest items, apply markers, and provide fixtures. |
| Test specification | [`config.py`](config.py) | Parse and validate replay metadata into `ReplaySpec` objects. |
| Test execution | [`replay.py`](replay.py) | Translate a replay manifest into services, run them in order, and apply the manifest's checks. |
| Services | [`services/`](services/) | Build, start, and validate each kind of process: ATS, microDNS, Proxy Verifier, origins, curl. Both test styles use them. |
| Runtime primitives | [`runtime.py`](runtime.py), [`process.py`](process.py), and [`expectations.py`](expectations.py) | Discover programs, allocate ports and sandboxes, own subprocesses, and express expected output. |

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
6. `ReplayTest.run()` creates the sandbox and starts the declared services:
   microDNS, verifier-server, ATS, and verifier-client. It then validates
   files, metrics, logs, and process output, and finally stops every process.

`config.py` also contains small ATS-configuration serialization helpers used
by `services/ats.py`, such as flat-record conversion and YAML writing. Those
helpers operate on data; process orchestration lives in `services/` and
`replay.py`.

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

## How the two styles share services

Both test styles launch each kind of process through one implementation in
`services/`, so a replay manifest and a procedural test asking for the same
process get the same command line, run root, readiness check, and output
validation. Dependencies point one way: `replay.py` uses `services/`, and
`services/` never imports `replay.py`.

| Process | Service | Procedural tests get it from | `ReplayTest` builds it with |
|---|---|---|---|
| Traffic Server | `ATS` | `ats`, `ats_factory` fixtures | `ATS.from_manifest()` |
| microDNS | `DNSServer` | `ServiceFactory.dns()` | `DNSServer.create()` |
| verifier-server | `VerifierServer` | `ServiceFactory.verifier_server()` | `VerifierServer.create()` |
| verifier-client | `VerifierClient` | `ServiceFactory.verifier_client()` | `VerifierClient.create()` |

`ATS` stages its configuration in the schema of a manifest's `urtest.ats`
mapping, whether a procedural test fills it through methods such as `records`
and `remap_config` or `ATS.from_manifest()` copies a manifest's mapping. Its
`start()` builds the run root from that one shape.

`ReplayTest` keeps only what is specific to a manifest: translating manifest
keys into service options, the ports of the processes ATS proxies to (passed
to ATS with `define_placeholders()`), the order processes start and stop in,
and the manifest's `file_checks`, `metric_checks`, and `log_validation` rules.

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

CI shards are assigned by a CRC32 hash of each test id, so a test keeps its
shard when other tests are added or removed. Rerunning one failed shard reruns
the same tests.

### `process.py`

Defines the low-level `ManagedProcess` primitive and `ProcessError`.
`ManagedProcess` owns subprocess startup, readiness polling, signals, captured
stdout and stderr, explicit stream and return-code expectations, timeouts, and
cleanup.

### `replay.py`

Defines the direct-replay execution engine. It builds the manifest's ATS,
microDNS, and Proxy Verifier processes through `services/`, starts them in
order, runs the manifest's lifecycle commands, and applies its file, metric,
and log checks.

### `runner.py`

Implements the Python entry point used by `tests/urtest.sh` and generated
build-tree wrappers. It separates wrapper options from pytest arguments,
selects Apple container, Podman, Docker, or direct execution, prepares the
container build, handles CI shards, chooses a short sandbox path, and restores
artifact ownership.

### `runtime.py`

Defines `TestRuntime`, the immutable description of one installed test
environment. It discovers ATS and helper programs, reads build features and
layout, allocates ports, creates readable per-test sandboxes, resolves
generated artifacts, and coordinates execution that must be exclusive.

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
| [`ats.py`](services/ats.py) | ATS run roots, configuration, placeholders, lifecycle, tools, diagnostics, and `ATSFactory`, for both test styles. |
| [`context.py`](services/context.py) | `ProceduralContext` sandbox state and `CommandResult` values. |
| [`curl.py`](services/curl.py) | Curl execution and ATS-aware TCP or Unix-domain-socket transport. |
| [`process_service.py`](services/process_service.py) | Service wrapper around `ManagedProcess`, adding readiness and output validation. |
| [`origin.py`](services/origin.py) | Microserver lifecycle and programmatic request/response sessions. |
| [`dns.py`](services/dns.py) | MicroDNS zone files, lifecycle, readiness, and record updates, for both test styles. |
| [`httpbin.py`](services/httpbin.py) | `go-httpbin` lifecycle and readiness. |
| [`verifier.py`](services/verifier.py) | Proxy Verifier server and client command lines, readiness, and violation checks, for both test styles. |
| [`service_factory.py`](services/service_factory.py) | Creation and cleanup of non-ATS services and arbitrary support processes. |
| [`service_utils.py`](services/service_utils.py) | File polling, TCP requests, and metric polling. |

The distinction between the two process layers is intentional:

- `process.py::ManagedProcess` is the general subprocess primitive.
- `services/process_service.py::ProcessService` adds readiness and output
  validation for support services, whether a replay or a procedural test
  started them.

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
- `test_plugin.py` covers pytest marker, manual-test, and shard selection behavior.
- `test_process.py` covers subprocess handling and placeholder replacement.
- `test_runner.py` covers wrapper arguments, containers, sharding, builds, and
  sandbox copying.
- `test_services.py` covers the public service facade, ATS ownership, cleanup,
  Curl argument parsing, and Unix-domain-socket behavior.

## Where new code belongs

- Put replay metadata parsing or validation in `config.py`.
- Put pytest collection, selection, markers, or fixtures in `plugin.py`.
- Put how a process is built, started, or validated in its `services/` module,
  so both test styles get it.
- Put manifest-only behavior, such as translating manifest keys, start order,
  and manifest-declared checks, in `replay.py`.
- Put environment discovery, ports, or sandbox allocation in `runtime.py`.
- Put generic subprocess behavior in `process.py`.
- Put framework regression tests in `tests/`.
