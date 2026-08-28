# Uranium Tests

This directory contains Apache Traffic Server's Uranium system tests and their
supporting tools. See the
[developer guide](../doc/developer-guide/testing/uranium-tests.en.rst) for the
complete authoring guide.

## Layout

- `uranium_tests/` contains tests collected by pytest. Direct Proxy Verifier
  tests use the `<scenario>.test.yaml` suffix. Native procedural tests use
  pytest's `test_*.py` convention.
- `tools/uranium/` is the native pytest testkit: replay collection, isolated
  ATS processes, support services, curl, and runtime helpers.
- `include/` contains headers used by test plugins and unit tests.

## Basic setup

Enable the suite when configuring ATS:

```console
cmake -B build -DENABLE_URTEST=ON
```

This builds the required plugins and helper tools and creates the Uranium test
targets.

## Running tests

Pytest owns the complete inventory. Run it with:

```console
cmake --build build -t urtest
```

To run only direct replay tests:

```console
cmake --build build -t urtest-replay
```

Set `URTEST_OPTIONS` when configuring to pass ordinary pytest selection and
concurrency arguments:

```console
cmake -B build -DENABLE_URTEST=ON -DURTEST_OPTIONS="-n 4 -k cache"
```

### The `urtest.sh` entry point

`tests/urtest.sh` is the portable source-tree entry point. On a normal host it
starts the official `ci.trafficserver.apache.org/ats/fedora:44` image with
Apple container on macOS, Podman on Linux, or Docker as a fallback. It builds
incrementally in `build-urtest-container` and runs the tests there. In a
Fedora 44 test environment it runs directly, which is also the Jenkins CI
behavior.

Force either mode when needed:

```console
./tests/urtest.sh --run-in-container
./tests/urtest.sh --no-run-in-container
```

The older `--run-in-docker` and `--no-run-in-docker` spellings remain aliases.

The generated `<build>/tests/urtest.sh` entry point is fastest when ATS is
already built and installed in the current environment.

### Selecting tests

Pass pytest's `-k` expression through the wrapper:

```console
./tests/urtest.sh -q -k header_rewrite
./tests/urtest.sh -q -k "header_rewrite or cache_control"
```

The expression selects direct `.test.yaml` items and native Python items the
same way.

### Running in parallel

Use pytest-xdist's `-n` option:

```console
./tests/urtest.sh -q -n 8
```

Each worker uses an isolated sandbox and dynamically allocated listeners.
Tests listed in `serial_tests.txt` take an exclusive execution lock and cannot
overlap any other Uranium item.

### Running manual tests

Native tests marked `@pytest.mark.manual` and replay tests with a true or
reason-string `urtest.manual` value are visible as skipped items in ordinary
runs but execute only when explicitly requested. Select the intended test while
enabling them:

```console
./tests/urtest.sh --run-manual -k ats_probe
```

Manual tests may still skip when their runtime requirements are unavailable,
such as root privileges, bpftrace, QUIC, or network-namespace support.

## Writing tests

Prefer a direct replay test when Proxy Verifier can provide both client and
server behavior. A `.test.yaml` file keeps ATS configuration, traffic, and
validation together and is independently schedulable.

Use a native `test_*.py` scenario when the test needs a custom curl, OpenSSL,
raw-socket, or Python client; a specialized server; multiple ATS instances; or
ordered runtime mutations. Structure these tests around a scenario class with
named configuration and phase methods and an explicit `run()` entry point.

The native fixtures are imported from `tools.uranium.services`. The most common
ones are `ATSFactory`, `ServiceFactory`, and `Curl`. They own process cleanup,
port allocation, and item sandboxes. Do not use fixed ports or global temporary
paths, because the full suite is expected to work with `-n`.

`tools.uranium.services` is the stable public import facade. Its implementations
are separated by responsibility under `tests/tools/uranium/services/` into
`ats.py`, `curl.py`, `origin.py`, `dns.py`, `httpbin.py`, `verifier.py`,
`process_service.py`, and `service_factory.py`.

`Curl.run()` and `Curl.run_for()` accept one shell-style argument string;
`Curl.get()` uses the same form for `options`. Values containing whitespace can
be quoted normally. The string is parsed with `shlex.split` and passed directly
to curl without invoking a shell.
