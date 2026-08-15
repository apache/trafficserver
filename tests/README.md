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
starts the official `ci.trafficserver.apache.org/ats/fedora:44` image, builds
incrementally in `build-urtest-container`, and runs the tests there. In a
Fedora 44 container it runs directly, which is also the Jenkins CI behavior.

Force either mode when needed:

```console
./tests/urtest.sh --run-in-docker
./tests/urtest.sh --no-run-in-docker
```

The generated `<build>/tests/urtest.sh` entry point is fastest when ATS is
already built and installed in the current environment.

### Selecting tests

Pass pytest's `-k` expression through the wrapper:

```console
./tests/urtest.sh -q -k cache_auth
./tests/urtest.sh -q -k "cache_auth or cache_control"
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
