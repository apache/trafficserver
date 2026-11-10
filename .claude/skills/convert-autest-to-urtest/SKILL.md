---
name: convert-autest-to-urtest
description: Convert Apache Traffic Server AuTests to pytest Uranium tests while preserving coverage, assertions, and process lifecycle. Use for explicit test migrations and for newly added or updated AuTests encountered during a requested Uranium-branch rebase.
---

<!--
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements. See the NOTICE file
distributed with this work for additional information
regarding copyright ownership. The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License. You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied. See the License for the
specific language governing permissions and limitations
under the License.
-->

# Convert AuTest to Uranium

Produce equivalent, maintainable tests—not merely a passing replacement.
This skill does not authorize rebases, commits, pushes, or production fixes
beyond the user's request.

## Establish the current framework

Read the repository's applicable instructions and these current sources:

- `doc/developer-guide/testing/uranium-tests.en.rst`: replay schema and execution.
- `doc/developer-guide/testing/uranium-api.en.rst`: fixtures and service APIs.
- `tests/tools/uranium/README.md`: framework responsibilities and code flow.
- The relevant implementations in `tests/tools/uranium/services/` and a nearby
  converted test with similar behavior.

Treat those sources as authoritative if API details below have changed. Do not
restore an AuTest compatibility backend or follow older AuTest authoring advice
where it conflicts with the migration. Obtain the original test from Git when
its working-tree copy has already been deleted or moved.

## Inventory behavior before translating

For each original test, account for:

- Every case, parameter variation, capability gate, and manual-only condition.
- Process startup dependencies, readiness, shutdown, and state shared between runs.
- Requests and wire behavior, including malformed traffic, timing, and half-closes.
- Return codes, positive and negative output assertions, gold files, diagnostics,
  metrics, and access-log counts.
- Helpers, certificates, plugin builds, replay files, and other required assets.

Map each original assertion to its replacement. Inspect what the old assertion
actually checked; a historical green result can be a false positive. Do not
weaken expectations, add an xfail, or replace executable coverage with a skip to
make a conversion pass. Report a demonstrated production bug separately if its
fix is outside the requested scope.

## Choose the test form

Prefer a self-contained `<scenario>.test.yaml`: put Proxy Verifier traffic and
the `urtest:` orchestration in the same file. Basic curl and microserver tests
often fit this form. Remove an old replay wrapper only after its configuration,
assertions, and lifecycle have all been represented. Avoid introducing a separate
replay file unless it genuinely needs to be shared.

Use a native `test_*.py` when special client/server behavior is necessary, such
as raw malformed messages, controlled disconnects, TLS session save/resume,
live reconfiguration, or functionality Proxy Verifier cannot express. Use plain
`test_*` functions taking fixtures, small helper functions, and parametrization
for independent variations. Classes are appropriate for stateful services, not
mandatory scenario wrappers with a `run()` entry point.

Preserve intentional state across cache priming/hit checks, session resumption,
and reload sequences. Splitting these into independent fixture instances changes
the test. Follow the surrounding directory layout and update asset paths.

## Preserve the difficult semantics

### Process lifetime and synchronization

- Compare AuTest run boundaries with Uranium cleanup explicitly. AuTest may stop
  a per-run origin before later metric checks; Uranium may keep it alive until
  final cleanup. For direct replays that require this, the current
  `urtest.server.stop_after_client: true` stops the origin before metrics/file
  checks. Do not enable it indiscriminately. Preserve crash detection before an
  intentional stop and validate process output afterward.
- Wait for the exact content the assertion needs, not an arbitrary number of log
  lines or a fixed sleep. A client completing 13 requests does not mean all 13
  access-log entries have flushed. Metric checks may need one snapshot containing
  all expected values.
- For asynchronous reloads, establish completion of the root operation, not just
  a successful child task or a superficially successful CLI summary. Verify the
  relevant behavior before and after the change.
- Diagnose protocol hangs before increasing timeouts. Preserve framing,
  Content-Length, half-close/drain behavior, and the connection reuse that causes
  the condition under test.
- Use fixture-managed directories and allocated ports. Preserve serial/exclusive
  execution requirements; update `tests/serial_tests.txt` when paths change.

### Assertions and controls

- Preserve regex semantics. An old expression such as `PR.` must not become a
  literal substring check. Keep exact counts and exclusions where meaningful.
- Use the shared gold comparison in `tools.uranium.assertions` or stream
  `matches_gold()`; do not introduce another comparison implementation.
- Retain positive controls in rejection tests: successful same-SNI resumption
  alongside cross-SNI rejection, valid padded frames alongside invalid padding,
  or permitted connection reuse alongside forbidden reuse. A failed request
  alone may only prove the test setup is broken.
- Keep default ATS log validation unless the test intentionally produces errors.
  Both replay tests and `ATS.close()` in procedural tests fail on `ERROR:`,
  `FATAL:` or `Unrecognized configuration value` in diags.log, as AuTest's
  `MakeATSProcess` did. An AuTest that set `disable_log_checks` maps to
  `ats_factory.create(..., disable_log_checks=True)` or the replay
  `process_config`; `FATAL:` still fails a procedural test unless it uses
  `expect_start_failure()`. Relax only the cases the original relaxed. An
  original that excluded all but specific errors maps to `ats.allow_errors()`;
  do not blanket-disable diagnostics to get a pass.
- Preserve real implementations of intentionally disabled diagnostic tests with
  `@pytest.mark.manual` or `urtest.manual`, runnable with `--run-manual`.
  Keep capability-based skips distinct from manual selection.

### API translation pitfalls

- Use explicit `stdout`/`stderr` methods: `contains(expression, explanation)`,
  `excludes(expression, explanation)`, and `matches_gold(path)`. Do not use `+=`
  or replace the stream object. Use `reset()` only for intentional replacement.
- Use the ATS-aware curl interface so Unix-domain-socket configuration survives:
  `curl.run_for(ats, arguments_string, timeout=seconds)` or `curl.get(ats, ...)`.
  Curl accepts a shell-style argument string; do not assume every command API
  has that signature. Check `ATS.run` versus `ATS.run_shell` explicitly.
- Launch Python helpers with `sys.executable`, not a hardcoded `python3`, to keep
  them in the test interpreter's environment.
- Check actual configuration-copy path resolution and supported placeholders.
  Do not invent placeholder names or assume every directory named `replay_*`
  resolves relative assets like the conventional `replay/` or `replays/` folder.
- Use current feature/plugin requirement checks. Document function parameters
  with `:param name:` and timeout units in seconds.

## Additional audit during a requested rebase

Compare upstream test changes between the previous base and the new base,
including modified existing tests—not only newly added files. Rename/delete
conflicts from `gold_tests` to `uranium_tests` require a semantic comparison;
blindly selecting either side can silently discard coverage.

Retain new helpers and assets, migrate new assertions/configuration into existing
Uranium tests, and update test-plugin CMake registrations and serial-test paths.
Search the entire test tree for leftover wrappers:

```bash
rg --files tests -g '*.test.py'
```

Review matches individually. Do not search only the migrated directory: a newly
added upstream directory can remain under `gold_tests`. Do not replace this with
a broad ban on all Python filenames ending in `test.py`.

## Verify and report

Use the repository's build and test documentation when building or running ATS.
If using an available build/test skill, prefer the current Uranium wrapper and
its help over older AuTest flags. Respect the user's chosen environment; discover container mount and build
paths rather than assuming a previous task's paths apply.

Run from the configured build's `tests` directory. For an already-running test
container, an example is:

```bash
./urtest.sh --no-run-in-container --collect-only -k '<expression>'
./urtest.sh --no-run-in-container -v -n 8 -k '<expression>'
```

Use pytest `-k` for filtering and `-n` for parallelism, not legacy `-f`/`-j`.
Confirm collection includes the intended cases and execution did not merely skip
them. Test changed cases plus controls affected by shared helpers. Repeat timing-
sensitive cases when warranted; report failures rather than rerunning until a
single green run conceals them. Run a full suite when requested or proportionate
to a broad change.

For framework changes, add focused harness tests for both existing/default and
new behavior. Run applicable replay validation, self-contained replay, native
inventory, and no-wrapper checks in `tests/tools/uranium/tests/`; do not hardcode
inventory counts. Format changed code and build docs when documentation changes.

Summarize converted cases, semantic differences or newly exposed bugs, exact
verification results, and any untested capabilities. Preserve requested failure
artifacts or inspection sandboxes; clean only task-owned scratch artifacts when
appropriate. Follow separate commit/push instructions only when authorized.
