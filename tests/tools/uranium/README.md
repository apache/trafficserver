# Uranium Pytest Framework

Uranium (urtest) is a pytest plugin plus a launcher. Pytest still collects,
schedules, and reports. Uranium teaches it two things: how to treat a YAML
replay file as a test, and how to give a Python test function live Traffic
Server processes through fixtures.

This directory is the framework. The tests themselves live in
`tests/uranium_tests`. See the [test-suite README](../../README.md) for
commands, the
[authoring guide](../../../doc/developer-guide/testing/uranium-tests.en.rst)
for writing tests, and the
[API reference](../../../doc/developer-guide/testing/uranium-api.en.rst) for
the fixture and service classes.

## From pytest to Uranium

Nothing in Uranium replaces pytest. `tests/conftest.py` registers it as a
plugin (`pytest_plugins = ["tools.uranium.plugin"]`) and puts `tests/` on
`sys.path`, and it uses pytest's standard extension points:

| pytest concept | What Uranium does with it | Where |
|---|---|---|
| Plugin | `plugin.py`, registered from `tests/conftest.py`. | `conftest.py` |
| Collector (`pytest.File`) | `ReplayFile` turns each `*.test.yaml` into test items. Python files are collected by stock pytest. | `plugin.py` |
| Item (`pytest.Item`) | `ReplayItem`: one per replay file, or one per variant in that file. Its `runtest()` runs the whole replay. | `plugin.py` |
| Fixtures | `ats`, `ats_factory`, `curl`, `services`, `procedural_context`, `uranium_test_runtime`. Each owns its processes and stops them at teardown. | `plugin.py` |
| Markers | `uranium_replay`, `uranium_procedural`, `serial`, `manual`, `uranium_sandbox_collision`. | `plugin.py` |
| `tmp_path` | A named per-test sandbox under the session sandbox root, deleted on success and kept on failure. | `runtime.py` |
| pytest-xdist | Runs tests in parallel. Uranium adds cross-worker port allocation, a shared/exclusive execution lock, and serial scheduling. | `runtime.py`, `plugin.py` |
| Command-line options | `--ats-bin`, `--proxy-verifier-bin`, `--build-root`, `--sandbox`, `--keep-sandboxes`, `--run-manual`, `--curl-uds`, `--urtest-shard-index`/`--urtest-shard-count`. | `plugin.py` |

In this codebase, *artifacts* means files a test produces or needs, such as
test plugins built by CMake and found with `TestRuntime.resolve_artifact()`.
What a test function receives as arguments are fixtures.

## File map

```
tests/
├── urtest.sh            # source-tree entry: builds, then runs
├── urtest.sh.in         # CMake template for build/tests/urtest.sh
├── CMakeLists.txt       # urtest targets and test plugins
├── conftest.py          # loads tools.uranium.plugin
├── pyproject.toml       # Python dependencies
├── uv.lock              # pinned Python environment
├── serial_tests.txt     # tests that must run alone
├── tools/uranium/       # the framework (this directory)
│   ├── runner.py
│   ├── plugin.py
│   ├── config.py
│   ├── replay.py
│   ├── runtime.py
│   ├── process.py
│   ├── expectations.py
│   ├── assertions.py
│   ├── utils.py
│   ├── services/        # one implementation of each process
│   ├── min_cfg/         # baseline ip_allow.yaml and storage.yaml
│   └── tests/           # framework self-tests
├── tools/               # Proxy Verifier, microserver, microDNS, test plugins
└── uranium_tests/       # the tests themselves
```

## Six layers

Each layer calls downward. The two test styles enter at different layers, a
replay through `replay.py` and a procedural test through fixtures, and both
reach the same services.

| Layer | Module | Responsibility |
|---|---|---|
| 1. Command entry point | [`runner.py`](runner.py) | Pick the host or a container, build the pytest command, and run it. |
| 2. Pytest integration | [`plugin.py`](plugin.py) | Collect replay files, apply markers, shard, schedule serial tests, and provide fixtures. |
| 3. Replay engine | [`replay.py`](replay.py) | Translate a manifest into services, start them in order, and apply the manifest's checks. |
| 4. Test specification | [`config.py`](config.py) | Parse and validate a replay manifest into `ReplaySpec` objects, one per variant. |
| 5. Services | [`services/`](services/) | Build, start, and validate each kind of process. Both test styles use them. |
| 6. Runtime primitives | [`runtime.py`](runtime.py), [`process.py`](process.py), [`expectations.py`](expectations.py) | Discover programs, allocate ports and sandboxes, own subprocesses, and express expected output. |

Imports only point down. In particular, `plugin.py` and `replay.py` use
`config.py` and `services/`, and nothing in `services/` imports `runner.py`,
`plugin.py`, `config.py`, or `replay.py`. The services write ATS's
configuration files with their own helpers in `services/ats_config.py`, so
`config.py` stays a manifest parser. A framework test imports each service
module in a fresh interpreter and fails if any of those higher layers is
loaded with it.

## One implementation per process

A replay manifest and a procedural test that ask for the same process get the
same command line, run root, readiness wait, and output checks, because both
build it through the same service class.

| Process | Service class | Procedural tests | `ReplayTest` |
|---|---|---|---|
| Traffic Server | `ATS` | `ats`, `ats_factory` fixtures | `ATS.from_manifest()` |
| microDNS | `DNSServer` | `services.dns()` | `DNSServer.create()` |
| verifier-server | `VerifierServer` | `services.verifier_server()` | `VerifierServer.create()` |
| verifier-client | `VerifierClient` | `services.verifier_client()` | `VerifierClient.create()` |

**ATS stages configuration in the manifest's shape.** Methods such as
`ats.records.update()` and `ats.remap_config.add_line()` fill the same mapping
a manifest's `urtest.ats` section holds; `ATS.from_manifest()` deep-copies a
manifest's mapping into it instead. `start()` builds the run root from that one
shape, so there is no second ATS code path. A framework test configures one
ATS each way and checks that `start()` receives identical mappings.

**`ReplayTest` owns only what a manifest adds:** translating `urtest.*` keys
into service options; the origin and DNS ports ATS proxies to, handed over
with `ats.define_placeholders()` and `ats.use_nameserver()`; the start and stop
order; and the manifest's `file_checks`, `metric_checks`, and `log_validation`
rules.

## Layer 1: from `urtest.sh` to pytest

There are two thin shell wrappers; all logic is in `runner.py`, which takes a
mode as its first argument.

- **`tests/urtest.sh` (source mode)** is checked in. It builds ATS with the
  urtest CMake preset into `build-urtest-container/`, installs it, and runs the
  generated wrapper. Afterward it copies the sandbox into the build tree and
  restores file ownership.
- **`build/tests/urtest.sh` (configured mode)** is generated by CMake from
  `urtest.sh.in`, with the source root, build root, install prefix, Proxy
  Verifier binary, and sandbox baked in. The `urtest` and `urtest-replay`
  CMake targets call it.

`runner.py` then:

1. **Picks an environment.** It runs directly, or relaunches inside the CI
   image (`ci.trafficserver.apache.org/ats/fedora:44`) with Apple container,
   Podman, or Docker. `--no-run-in-container` forces the host, and the
   `ATS_URTEST_IN_CONTAINER` marker prevents nested launches.
2. **Translates arguments.** Wrapper options are separated from pytest
   options, and CI shard environment variables become
   `--urtest-shard-index`/`--urtest-shard-count`.
3. **Builds the pytest command:** `uv run pytest tools/uranium/tests
   uranium_tests --ats-bin=… --proxy-verifier-bin=… --build-root=…
   --sandbox=…`. The framework self-tests are always collected alongside the
   scenarios. Inside the CI image it enables the `fast-diff` extra
   (`cdifflib`).
4. **Prepares the environment.** It prepends the install `lib/` to the
   library path, blanks proxy variables so curl never leaves loopback, and
   disables `.pyc` writes.

## Layer 2: the pytest hooks, in session order

| Hook | What it does |
|---|---|
| `pytest_addoption` | Registers the Uranium options. |
| `pytest_configure` | Declares the Uranium markers. |
| `pytest_sessionstart` | On the xdist controller only: takes an exclusive lock on `<sandbox>/.session-lock` so two runs cannot share a sandbox, and resets `.port-counter`. |
| `pytest_collect_file` | Claims `*.test.yaml` and `*.test.yml` and returns a `ReplayFile`. Everything else is left to pytest. |
| `pytest_collection_modifyitems` | Marks `test_*.py` modules as `uranium_procedural`; detects two tests that would share a sandbox name; marks tests in `serial_tests.txt` as `serial` and groups them on one xdist worker; deselects `h2`, `tls`, and `tls_hooks` under `--curl-uds`; applies sharding; moves serial tests to the end. |
| `pytest_runtest_setup` | Fails any item with a sandbox collision. Raising a usage error inside an xdist worker would crash the controller, so the failure is deferred to here. |
| `pytest_runtest_makereport` | Deletes the test's sandbox if setup, call, and teardown all passed. `--keep-sandboxes` keeps all of them. |

- **Sharding:** `crc32(nodeid) % count == index`. Hashing the test id rather
  than its position keeps each test on the same shard when other tests are
  added or removed, so rerunning a failed shard reruns the same tests.
- **Serial tests** are listed by path in `serial_tests.txt`. They get the
  `serial` marker and an `xdist_group`, run last, and take the execution lock
  exclusively.
- **Manual tests**, a replay with `urtest.manual` or a function marked
  `@pytest.mark.manual`, are skipped unless `--run-manual` is passed.

## Test style 1: direct replay tests

One `*.test.yaml` holds two things. The usual Proxy Verifier keys (`meta`,
`sessions`) describe the traffic; alternatively, `urtest.replay` names a
separate traffic file. A `urtest:` mapping describes the processes, the ATS
configuration, and what to check. There is no Python in the test.

```yaml
# uranium_tests/connect_down_policy/replay/inactive_timeout.test.yaml (abridged)
sessions:
- transactions:
  - client-request: {method: GET, url: /inactive/timeout/test, …}
    server-response: {status: 200, delay: 10s}
    proxy-response:  {status: 504}

urtest:
  description: Apply connect-down policy after an inactive origin timeout
  server: {name: connect-down-policy-origin}
  client: {name: connect-down-policy-client}
  ats:
    name: connect-down-policy-ts
    process_config: {enable_cache: false}
    records_config:
      proxy.config.http.transaction_no_activity_timeout_out: 3
    remap_config:
    - map / http://127.0.0.1:{SERVER_HTTP_PORT}/
  variants:
  - name: mark-down
    ats:
      records_config: {proxy.config.http.connect.down.policy: 3}
      file_checks:
      - {path: '{LOG_DIR}/error.log', contains: marking down}
  - name: do-not-mark-down
    ats:
      records_config: {proxy.config.http.connect.down.policy: 2}
      log_validation:
        error_log: {excludes: [{expression: marking down, …}]}
```

This file becomes two pytest items, `inactive_timeout-mark-down` and
`inactive_timeout-do-not-mark-down`. Each variant is deep-merged over the base
`urtest` mapping.

From collection to execution:

1. **Collect.** `ReplayFile.collect()` calls `ReplaySpec.load_all()`. A bad
   manifest fails at collection with a precise message.
2. **Validate.** `config.py` requires `description`, `server`, `client`, and
   `ats`, checks `file_checks` keys, and confirms the traffic file exists.
3. **Gate.** `skip`, or unmet `requires` (programs, ATS features, plugins, or
   OpenSSL, Proxy Verifier, or curl versions), becomes a pytest skip.
4. **Allocate.** `ReplayTest.__init__` creates the unstarted ATS with
   `ATS.from_manifest()`, which reserves ATS's own ports, then reserves the
   origin ports and one deliberately unused port.
5. **Start.** A fresh sandbox, then an optional `DNSServer`, the
   `VerifierServer`, and `ATS.start()` (run root built, placeholders
   replaced, readiness awaited), then `pre_client_commands` through
   `ats.run_commands()`.
6. **Drive.** A `VerifierClient` replays the sessions through ATS and is
   checked for violations when it exits; then `post_client_commands` run.
7. **Check.** Background processes still alive, metrics, and `file_checks`;
   then every process is stopped in reverse order; then server output,
   `ATS.validate_diagnostics()`, and the manifest's `log_validation` rules.

### Placeholders

Ports and paths are not known until the test runs, so manifests use these
tokens anywhere in ATS configuration, commands, and file checks.
`ATS.replace_placeholders()` expands its own ports and directories;
`ReplayTest` adds the origin, DNS, and unused ports with
`define_placeholders()`.

| Kind | Tokens |
|---|---|
| ATS ports | `{ATS_HTTP_PORT}`, `{ATS_HTTPS_PORT}`, `{ATS_PROXY_PROTOCOL_PORT}`, `{ATS_PROXY_PROTOCOL_HTTPS_PORT}` |
| Other ports | `{SERVER_HTTP_PORT}`, `{SERVER_HTTPS_PORT}`, `{SERVER_EXTRA_HTTP_PORT}`, `{DNS_PORT}`, `{UNUSED_PORT}` |
| Paths | `{ATS_ROOT}`, `{CONFIG_DIR}`, `{LOG_DIR}`, `{RUNTIME_DIR}`, `{STORAGE_DIR}`, `{ATS_SSL_DIR}`, `{TEST_DIR}`, `{PYTHON}`, `{PATH}` |

## Test style 2: procedural tests

When a scenario needs custom clients, raw sockets, runtime configuration
changes, or several ATS instances, write an ordinary `test_*.py`. Ask for
fixtures by argument name, configure objects that have not started yet, start
them, and assert:

```python
from tools.uranium.services import ATS, Curl


def test_traffic_server_starts(ats: ATS, curl: Curl) -> None:
    ats.start()
    result = curl.get(ats)
    assert result.returncode == 0, result.output
```

There is no teardown code: the fixtures stop every process they created and
validate the output expectations each one registered.

```
uranium_test_runtime          (session: TestRuntime)
└── procedural_context        (lock + sandbox per test)
    ├── ats_factory           (ATSFactory)
    │   └── ats               (ats_factory.create("ats"))
    ├── services              (ServiceFactory)
    └── curl                  (Curl)
```

Everything below `procedural_context` is function-scoped: created per test and
closed at teardown.

- **`procedural_context`** takes the execution lock (exclusive for `serial`
  tests, shared otherwise) and creates a `ProceduralContext`: the runtime, the
  test's sandbox (`run_directory`), its source directory, and whether curl
  should use a Unix socket.
- **`ATS`** is an unstarted proxy with staged configuration, the same class
  replay tests use. Before `start()`, edit `ats.records`,
  `ats.remap_config.add_line(…)`, `plugin_config`, `ip_allow_config`, and
  others, or copy files in. After it, call `traffic_ctl()`, `rpc()`,
  `send_signal()`, or `drain_and_stop()`, and read `diags_log` or
  `error_log`. `expect_start_failure()` and `allow_errors()` cover negative
  tests.
- **`ATSFactory`** creates additional named ATS instances for multi-proxy
  topologies such as parent chains, and closes all of them at teardown.
- **`ServiceFactory`** creates everything that is not ATS: `origin()`
  (microserver), `dns()` (microDNS), `httpbin()`, `verifier_server()`,
  `verifier_client()`, and arbitrary `process()` commands. It also exposes
  `allocate_port()`.
- **`Curl`** runs curl against an ATS instance. `get(ats)` and
  `run_for(ats, args)` target the right port, or the ATS Unix socket under
  `--curl-uds`.
- **`service_utils`** has polling helpers: `wait_for_file_lines()`,
  `wait_for_metric()`, and `send_tcp()`.

## Layer 6: processes and expectations

Every subprocess goes through one primitive:

- `process.py::ManagedProcess` owns startup, readiness polling, signals,
  captured stdout and stderr, return-code expectations, timeouts, and cleanup.
- `services/process_service.py::ProcessService` wraps it with readiness and
  output validation. `DNSServer`, `VerifierServer`, `VerifierClient`, and the
  other support services subclass it. `ATS` owns its `ManagedProcess`
  directly.

Output expectations are declared up front, each with an explanation:

```python
origin.stdout.contains("GET /redirect-0", "origin saw the redirect")
ats.stderr.excludes("FATAL", "no fatal diagnostics")
client.stdout.matches_gold("gold/http-204.gold")
client.expect_return_codes(0, 1)
```

`expectations.py::StreamExpectations` collects these, and they are checked
when the process completes or is stopped. The AuTest-style `+=` form raises an
error, so every check is an explicit call.

`assertions.assert_matches_gold()` is shared by both test styles. It uses
`cdifflib` when the `fast-diff` extra is installed and falls back to `difflib`
otherwise. Relative gold paths resolve against the test's source directory,
not the sandbox.

## Layer 6: runtime and isolation

`TestRuntime` is a frozen description of the installed environment, built
once per pytest process by `get_runtime()`. It checks that `traffic_server`,
`traffic_ctl`, `traffic_layout`, and both verifier binaries exist, then asks
`traffic_layout` for the install layout, compiled features, and library
versions. Requirement checks and plugin paths come from those answers.

These let hundreds of ATS instances run in parallel:

| Mechanism | What it does |
|---|---|
| `.session-lock` | Exclusive lock held by the pytest controller for the whole run. A second run pointed at the same `--sandbox` fails at once. |
| `.port-counter` | The last port handed out, protected by a lock and shared by every xdist worker. `allocate_port()` increments it, probes the port with a real `bind()`, and skips ports in use, within 10001–29999. |
| `.execution-lock` | Readers–writer lock. Normal tests take it shared; serial tests take it exclusive, so they wait for running tests and block new ones. |
| Sandbox name | Derived from the pytest node id, minus the file path, with unsafe characters replaced. Collisions are detected at collection. |
| `prepare_sandbox()` | Refuses to delete any path outside the sandbox root, then recreates the directory empty. |
| `resolve_artifact()` | Expands `{AtsTestPluginsDir}`, `{AtsBuildUraniumTestsDir}`, `{RepoDir}`, and environment variables, then looks next to the test source and then in the build-tree mirror of `uranium_tests/`. This is how test plugins built by CMake are found. |
| `min_cfg/` | The baseline `ip_allow.yaml` and `storage.yaml` copied into every ATS configuration directory before test-specific configuration is written. |

```
<sandbox>/                          # --sandbox, a short path by default
├── .session-lock
├── .port-counter
├── .execution-lock
├── inactive_timeout-mark-down/     # a replay item, kept only if it failed
│   ├── connect-down-policy-ts/     # ATS root: bin/ config/ log/ plugin/ ssl/ storage/ …
│   ├── connect-down-policy-origin.stdout
│   └── connect-down-policy-client.stdout
└── test_deny_any_address/          # a procedural item
    ├── ats/
    ├── redirect-origin/
    └── dns/
```

## Where new framework code belongs

| Change | Module | Covered by |
|---|---|---|
| New `urtest:` key or validation | `config.py` | `tests/test_config.py` |
| Collection, markers, sharding, fixtures | `plugin.py` | `tests/test_plugin.py` |
| How any process is built, started, or validated | `services/` | `tests/test_services.py` |
| Manifest-only behavior: key translation, start order, manifest checks | `replay.py` | `tests/test_replay.py` |
| Program discovery, ports, sandboxes | `runtime.py` | `tests/test_replay.py`, `tests/test_plugin.py` |
| Generic subprocess behavior | `process.py` | `tests/test_process.py` |
| Wrapper options, containers, builds | `runner.py` | `tests/test_runner.py` |
| Gold-file comparison | `assertions.py` | `tests/test_assertions.py` |
| Address, TCP-readiness, and version helpers | `utils.py` | `tests/test_utils.py` |

The framework self-tests under `tests/` are fast and do not start ATS.
`runner.py` always passes that directory to pytest, so a framework regression
shows up in every urtest run.
