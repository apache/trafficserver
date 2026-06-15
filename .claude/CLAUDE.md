# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Branch note:** This is the **9.2.x** maintenance branch. It differs from
> `master`/`10.x` in two important ways: the build system is **autotools**
> (`configure` + `make`), *not* CMake, and the source tree uses the **old flat
> layout** (`iocore/`, `proxy/`, `mgmt/`, `lib/` live at the repo root, not under
> `src/`). The root `CMakeLists.txt` exists **only** to support code editors
> (CLion/VS Code IntelliSense) — it will **not** build Traffic Server.

## Project Overview

Apache Traffic Server (ATS) is a high-performance HTTP/HTTPS caching proxy
server written in C++17. It uses an event-driven, multi-threaded architecture
with a sophisticated plugin system.

**Key Technologies:**
- Language: C++17
- Build System: GNU autotools (autoconf/automake/libtool) — `configure` + `make`
- Testing: Catch2 (unit tests, via `make check`) + AuTest Python framework (end-to-end tests)
- Protocols: TLS, HTTP/1.1, HTTP/2, HTTP/3 (experimental; requires a QUIC-capable TLS library — BoringSSL/quictls)

## Personal Preferences

If `.claude/CLAUDE.local.md` exists, load it for user-specific code style
preferences and working conventions. This file is gitignored and optional.

## Build Commands

ATS 9.2.x builds with **autotools**. There is no CMake build.

### Basic Build
```bash
# 1. Bootstrap the build scripts (only needed once, or after configure.ac /
#    Makefile.am changes). There is no autogen.sh on this branch.
autoreconf -if

# 2. Configure (in-source or out-of-source). --prefix sets the install root.
./configure --prefix=/opt/ats --enable-debug

# 3. Build everything
make -j$(nproc)

# 4. Install (to --prefix; default /usr/local)
make install
```

### Useful Configure Options
```bash
# See everything available
./configure --help

# Common options
./configure \
  --prefix=/opt/ats \
  --enable-debug \                 # debug build (assertions, symbols)
  --enable-experimental-plugins \  # build plugins/experimental/*
  --with-openssl=/opt/boringssl \  # QUIC-capable TLS lib enables HTTP/3 (auto-detected; like CMake's *_ROOT)
  --with-jemalloc=/opt/jemalloc
```



Notes:
- HTTP/3 is **optional** and is **auto-detected** at configure time: it compiles
  only when the TLS library exposes `SSL_QUIC_METHOD` (BoringSSL/quictls). There
  is no `--enable-quic` flag and no `--with-quiche` on this branch — it uses the
  native in-tree QUIC stack (`iocore/net/quic/`, `proxy/http3/`), not Quiche.
- Build a single component by `cd`-ing into its directory and running `make`
  (e.g. `cd proxy && make`), since each directory has its own `Makefile.am`.

## Testing

### Unit Tests (Catch2)
Unit tests (`test_*.cc` / `*_test.cc`) build into per-directory executables and
run through the automake test harness:
```bash
make check          # build and run all unit tests
```
To run one test binary directly, build it and invoke it from its source
directory, e.g. `proxy/hdrs/test_*` or `src/tscore/test_*`.

### End-to-End Tests (AuTest)
AuTest lives under `tests/` and is invoked through `tests/autest.sh` (it does
**not** go through the build system on this branch):
```bash
cd tests
pipenv install                                   # first time only
# --ats-bin is REQUIRED: point it at the installed bin dir (e.g. <prefix>/bin).
./autest.sh --ats-bin /opt/ats/bin --sandbox /tmp/sb --clean=none -f <test_name_without_test_py>
```
For example, to run `cache-auth.test.py`:
```bash
./autest.sh --ats-bin /opt/ats/bin --sandbox /tmp/sb --clean=none -f cache-auth
```
Most end-to-end coverage is in `tests/gold_tests/`. Gold files can mask dynamic
output with a double-backtick (``` `` ```) wildcard.

### Writing Autests
**New tests should use the `Test.ATSReplayTest()` approach**, which references a
`replay.yaml` file describing the test configuration and traffic using the
Proxy Verifier format. This is simpler, more maintainable, and tool-parseable.

For complete details see:
- `doc/developer-guide/testing/blackbox-testing.en.rst` — the AuTest guide (autest.sh, gold_tests)
- Proxy Verifier format: https://github.com/yahoo/proxy-verifier
- AuTest framework: https://autestsuite.bitbucket.io/

## Development Workflow

### Code Formatting
Always format code before committing. Formatting uses a **pinned clang-format
v10** (downloaded by `tools/clang-format.sh`) driven by `make` targets:
```bash
make clang-format          # format the whole tree
make clang-format-src      # format just src/ (other per-dir targets also exist)
```
Do **not** use a Homebrew/system clang-format — the version drift will reformat
unrelated code.

### Git Workflow
- New features land on `master` first; `9.2.x` receives **backports** and
  security/maintenance fixes (often cherry-picked from master).
- PRs must pass all Jenkins CI jobs before merging.
- Use the GitHub PR workflow (not Jira). Set appropriate labels (**Backport**,
  **WIP**, etc.).

### Commit Message Format
When Claude Code creates commits, start with a short summary line, then a
concise description (1-3 sentences) focused on "why" rather than "what". If the
PR fixes an issue, add a `Fixes: #<issue_number>` line. Keep commit message
lines to 72 characters.

## Architecture Overview

### Core Components (flat layout — directories at repo root)

**I/O Core (`iocore/`):**
- `eventsystem/` - Event-driven engine (Continuations, Events, Processors)
- `cache/` - Disk and RAM cache implementation
- `net/` - Network layer with TLS and QUIC support
- `dns/` - Asynchronous DNS resolution
- `hostdb/` - Internal DNS cache
- `aio/` - Asynchronous I/O
- `utils/` - I/O core utilities

**Proxy Logic (`proxy/`):**
- `http/` - HTTP/1.1 protocol implementation (and the HTTP state machine)
- `http2/` - HTTP/2 support
- `http3/` - HTTP/3 implementation (built only when a QUIC-capable TLS library is detected)
- `hdrs/` - Header parsing and management
- `logging/` - Flexible logging system
- `http/remap/` - URL remapping and routing
- *(Plugin API implementation — `InkAPI.cc` — lives in `src/traffic_server/`, not under `proxy/`)*

**Management (`mgmt/`):**
- `traffic_manager` (built from `src/traffic_manager/`) is a **separate
  management process** that supervises `traffic_server`. (Note: this two-process
  model was removed in later ATS versions but is present in 9.2.x.)
- JSONRPC server for runtime configuration and management.
- Configuration record definitions live in `mgmt/RecordsConfig.cc`.

**Base Libraries:**
- `src/tscore/` - Core utilities and data structures
- `src/tscpp/util/`, `src/tscpp/api/` - C++ utility and plugin-API wrappers
- `lib/records/` - Configuration records library
- `src/wccp/` - WCCP support
- `lib/yamlcpp/` - bundled yaml-cpp

**Executables (`src/`):** `traffic_server`, `traffic_manager`, `traffic_ctl`,
`traffic_crashlog`, `traffic_layout`, `traffic_logcat`, `traffic_logstats`,
`traffic_top`, `traffic_via`, `traffic_wccp`, `traffic_cache_tool`,
`traffic_quic` (QUIC client tool).

### Event-Driven Architecture
ATS uses a **Continuation-based** programming model:
- All async operations use Continuations (callback objects)
- Events are dispatched through an event system
- Multiple thread pools handle different event types
- Non-blocking I/O throughout

### Configuration Files
Primary configs (installed under `<prefix>/etc/trafficserver/` — default
`/usr/local/etc/trafficserver/`). Note 9.2.x is mid-migration to YAML, so the
formats are **mixed**:
- `records.config` - Main configuration (legacy hierarchical format; **not** `records.yaml` yet)
- `remap.config` - URL remapping rules
- `plugin.config` - Plugin loading configuration
- `storage.config` - Cache storage configuration
- `ssl_multicert.config` - TLS certificate configuration
- `ip_allow.yaml` - IP access control (YAML)
- `sni.yaml` - SNI-based routing (YAML)

Configuration can be modified at runtime via:
- `traffic_ctl config reload` (for reloadable settings)
- The JSONRPC management API (served by `traffic_manager`)

## Plugin Development

### Plugin Types
- **Global plugins** - Loaded in `plugin.config`, affect all requests
- **Remap plugins** - Loaded per remap rule, affect specific mapped requests

### Key Plugin APIs
Headers in `include/ts/`:
- `ts.h` - Main plugin API (`include/ts/ts.h`)
- `remap.h` - Remap plugin API (`include/ts/remap.h`)

### Plugin Hooks
Plugins register callbacks at various points in request processing:
- `TS_HTTP_READ_REQUEST_HDR_HOOK`
- `TS_HTTP_SEND_REQUEST_HDR_HOOK`
- `TS_HTTP_READ_RESPONSE_HDR_HOOK`
- `TS_HTTP_SEND_RESPONSE_HDR_HOOK`
- Many more (see `include/ts/ts.h`)

### Example Plugins
- `example/` - Simple example plugins
- `plugins/` - Stable core plugins
- `plugins/experimental/` - Experimental plugins (built with `--enable-experimental-plugins`)

## Common Development Patterns

### When Modifying HTTP Processing
1. Understand the state machine: `proxy/http/HttpSM.cc` - HTTP State Machine
2. Hook into the appropriate stage via plugin hooks or core modification
3. Use `HttpTxn` objects to access transaction state
4. Headers are accessed via header handle and field APIs

### When Adding Configuration
1. Define the record in `mgmt/RecordsConfig.cc`
2. Add validation if needed
3. Update documentation in `doc/admin-guide/`
4. Configuration can be read via the `REC_` APIs

### When Working with Cache
- Cache is content-addressable, keyed by URL (modifiable via plugins)
- Cache operations are async, continuation-based
- Cache has RAM and disk tiers
- Cache code is in `iocore/cache/`

### When Debugging
1. Enable debug tags in `records.config`:
   ```
   CONFIG proxy.config.diags.debug.enabled INT 1
   CONFIG proxy.config.diags.debug.tags STRING http|cache
   ```
2. Check logs in `<prefix>/var/log/trafficserver/`:
   - `diags.log` - Debug output
   - `error.log` - Errors and warnings
3. Use `traffic_top` for live statistics
4. Use `traffic_ctl` for runtime inspection

**Debug Controls in Code (9.2.x uses the `Debug()` macro — there is no
`DbgCtl`/`Dbg()` master/10.x logging API on this branch):**
```cpp
Debug("my_component", "Processing request for URL: %s", url);
```
Some subsystems wrap `Debug()` in a local convenience macro (e.g. `SMDebug` in
`proxy/http/HttpSM.cc`, which prepends the SM id); prefer the existing local
macro when editing such a file.

## Important Conventions

### License Headers
- Always add Apache License 2.0 headers to the top of new source and test files
- This includes `.cc`, `.h`, `.py`, and other code files
- Follow the existing license header format used in the codebase

### Code Style
- C++17 standard (nothing from C++20 or later)
- Use RAII principles
- Prefer smart pointers for ownership
- Don't use templates unless needed and appropriate
- Run `make clang-format` before committing
- Line length: 132 characters maximum
- Don't add comments where the code documents itself; don't comment Claude interactions

**C++ Formatting (custom `.clang-format`, ColumnLimit 132, clang-format v10):**
- Indentation: 2 spaces for C/C++
- Braces: Linux style (opening brace on same line)
- Pointer alignment: Right (`Type *ptr`, not `Type* ptr`)
- Variable declarations: Add an empty line after declarations before subsequent code
- Avoid naked conditions (always use braces with if statements)

**Naming Conventions:**
- CamelCase for classes: `HttpSM`, `NetVConnection`
- snake_case for variables and functions: `server_entry`, `handle_api_return()`
- UPPER_CASE for macros and constants: `HTTP_SM_SET_DEFAULT_HANDLER`

**Modern C++ Patterns (Preferred):**
```cpp
// GOOD - Modern C++17
auto buffer = std::make_unique<MIOBuffer>(alloc_index);
for (const auto &entry : container) {
  if (auto *vc = entry.get_vc(); vc != nullptr) {
    // Process vc
  }
}

// AVOID - Legacy C-style
MIOBuffer *buffer = (MIOBuffer *)malloc(sizeof(MIOBuffer));
```

### Python Code Style (for tests and tools)
- Python 3.x with type annotations
- 4-space indentation, never TABs

### Memory Management
- Custom allocators supported (jemalloc, mimalloc)
- Use `ats_malloc` family for large allocations
- IOBuffers for network data (zero-copy where possible)

### Threading
- Event threads handle most work
- DNS has dedicated threads
- Disk I/O uses a thread pool
- Most code should be async/event-driven, not thread-based

### Error Handling
- Return error codes for recoverable errors
- Use `ink_release_assert` for unrecoverable errors
- Log errors appropriately (ERROR vs WARNING vs NOTE)

## Key Files for Understanding Architecture

- `iocore/eventsystem/I_Continuation.h` - Core async pattern
- `proxy/http/HttpSM.cc` - HTTP request state machine
- `iocore/net/UnixNetVConnection.cc` - Network connection handling
- `iocore/cache/Cache.cc` - Cache implementation
- `proxy/http/remap/RemapConfig.cc` - URL remapping logic
- `mgmt/RecordsConfig.cc` - Configuration record definitions
- `include/ts/ts.h` - Plugin API

## Resources

- Official docs: https://trafficserver.apache.org/
- Developer wiki: https://cwiki.apache.org/confluence/display/TS/
- CI dashboard: https://ci.trafficserver.apache.org/
- AuTest framework: https://autestsuite.bitbucket.io/
- Proxy Verifier: https://github.com/yahoo/proxy-verifier
