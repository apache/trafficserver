# Uranium tests

Uranium runs Traffic Server integration tests with pytest. Tests live in
`uranium_tests/`; fixtures, services, the replay runner, and framework unit
tests live in `tools/uranium/`. See its [module guide](tools/uranium/README.md).
`include/` contains C++ test headers.

## Build and run on ATS 9.2

This branch uses Autotools, not CMake. From the repository root:

```sh
autoreconf -if
mkdir build-uranium
cd build-uranium
../configure --prefix=/path/to/ats-install --enable-experimental-plugins
make -j8
make install
cd tests
bash ./urtest.sh --no-run-in-container -v -n 8
```

Choose a writable install prefix. Install Python 3.11 or newer and `uv`; the
wrapper provisions Python dependencies from `pyproject.toml` and `uv.lock`.
Prepare Proxy Verifier with `tests/prepare_proxy_verifier.sh` in the source
tree, or set `PROXY_VERIFIER_PATH` to its client/server executable directory.
Protocol-specific tests also need external clients such as curl, nghttp, or
OpenSSL. Capability checks skip unavailable optional tools and features.

From the build root, `make -C tests urtest URTEST_OPTIONS="-v -n 8"` runs the
same suite; `make -C tests urtest-replay` selects replay tests only.
The configured `build-uranium/tests/urtest.sh` records the source, build, and
installation paths.

The source-tree `tests/urtest.sh` can build and run in the official Fedora test
container using Apple container, Podman, or Docker. Use `--run-in-container`
to request this explicitly. Inside an existing container, use the configured
build-tree wrapper with `--no-run-in-container`.

## Selecting tests and inspecting results

Help distinguishes wrapper options from arguments forwarded to pytest:

```sh
bash ./urtest.sh --help
bash ./urtest.sh -v -k 'header_rewrite or cache_control'
bash ./urtest.sh --collect-only -q
bash ./urtest.sh -n 8 --sandbox /tmp/ats-tests --keep-sandboxes -k test_basic
```

`-k` is pytest's substring/boolean selector; `-n` controls pytest-xdist workers.
Each test gets `<sandbox>/<test-name>/`. Failed-test sandboxes are retained;
`--keep-sandboxes` retains passing tests too. Rerunning a test replaces its
previous sandbox. Tests in `serial_tests.txt` run without overlap. Opt into
manual tests explicitly with `--run-manual -k <test-name>`.

## Writing tests

Prefer a self-contained `<scenario>.test.yaml`: its `urtest` section configures
ATS and support processes, and its Proxy Verifier sessions describe traffic
and assertions. For custom clients, servers, or runtime mutations, write a
plain `test_*.py` function taking fixtures such as `ats_factory`, `services`,
and `curl`. There is no AuTest compatibility backend.

On 9.2, records are written to typed `records.config`, not `records.yaml`.
Native `ats.records.update({...})` and replay `records_config` accept full
`proxy.config.*` names and handle serialization. Use `command="traffic_manager"`
for native ATS instances, or `ats.process_config.command: traffic_manager`
in a replay, for management reloads and manager-dependent metrics. Keep
configurations and assertions compatible with this branch's features.

See the [test guide](../doc/developer-guide/testing/uranium-tests.en.rst) and
[fixture/API reference](../doc/developer-guide/testing/uranium-api.en.rst).
