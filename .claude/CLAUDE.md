# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Apache Traffic Server (ATS) is a high-performance HTTP/HTTPS caching proxy
server written in C++20. It uses an event-driven, multi-threaded architecture
with a sophisticated plugin system.

**Key Technologies:**
- Language: C++20
- Build System: CMake (migrated from autotools in v10)
- Testing: Catch2 (unit tests) + AuTest Python framework (end-to-end tests)
- Protocols: TLS, HTTP/1.1, HTTP/2, HTTP/3 (via Quiche)

## Personal Preferences

If `.claude/CLAUDE.local.md` sub-agent exists, load it for user-specific code style
preferences and working conventions. This file is gitignored and optional.

## Build Commands

### Basic Build
```bash
# Configure (creates out-of-source build directory)
cmake -B build

# Build everything
cmake --build build

# Install (default: /usr/local)
cmake --install build
```

### Common Build Presets
```bash
# Development build with compile_commands.json for IDE support
cmake --preset dev
cmake --build build-dev

# Release build
cmake --preset release
cmake --build build-release

# Build with autests enabled
cmake --preset ci-fedora-autest
cmake --build build-autest
```

### Useful Build Options
```bash
# Specify dependency locations (like autotools --with-*)
cmake -B build -Djemalloc_ROOT=/opt/jemalloc -DOPENSSL_ROOT_DIR=/opt/boringssl

# Build specific targets
cmake --build build -t traffic_server
cmake --build build -t format  # Format code before committing
```

### Key CMake Options
- `BUILD_EXPERIMENTAL_PLUGINS=ON` - Enable experimental plugins
- `ENABLE_QUICHE=ON` - QUIC/HTTP3 support
- `ENABLE_CRIPTS=ON` - Cripts scripting API
- `BUILD_REGRESSION_TESTING=ON` - Enable test suite
- `ENABLE_ASAN=ON` - Configure ASan instrumentation

## Testing

### Unit Tests (Catch2)
Built automatically with the project. Run via ctest:
```bash
ctest --test-dir build -j4
```

### Running a Single Unit Test
Unit tests are built into executables. Find the test binary and run it directly:
```bash
# Unit tests are typically in build/src/<component>/
./build/src/tscore/test_tscore
```

### End-to-End Tests (AuTest)

**Enable autests during configuration:**
```bash
cmake -B build -DENABLE_AUTEST=ON
cmake --build build
cmake --install build
```

**Run all autests:**
```bash
cmake --build build -t autest
```

**Run specific test(s):**
```bash
cd build/tests
pipenv install  # First time only
./autest.sh --sandbox /tmp/sbcursor --clean=none -f <test_name_without_test_py>
```

For example, to run `cache-auth.test.py`:
```bash
./autest.sh --sandbox /tmp/sbcursor --clean=none -f cache-auth
```

Most end-to-end test coverage is in `tests/gold_tests/`. The CI system uses the
Docker image `ci.trafficserver.apache.org/ats/fedora:43` (Fedora version updated
regularly).

### Writing Autests

**New tests should use the `Test.ATSReplayTest()` approach**, which references a
`replay.yaml` file that describes the test configuration and traffic patterns
using the Proxy Verifier format. This is simpler, more maintainable, and
parseable by tools.

**For complete details on writing autests, see:**
- `doc/developer-guide/testing/autests.en.rst` - Comprehensive guide to autest
- Proxy Verifier format: https://github.com/yahoo/proxy-verifier
- AuTest framework: https://autestsuite.bitbucket.io/

## Development Workflow

### Code Formatting
Always format code before committing:
```bash
cmake --build build -t format
```

### Git Workflow
- Branch off `master` for all PRs
- PRs must pass all Jenkins CI jobs before merging
- Use the GitHub PR workflow (not Jira)
- Set appropriate labels: **Backport**, **WIP**, etc.

### Commit Message Format
When Claude Code creates commits, start with a short summary line, use concise
description (1-3 sentences) that focus on "why" rather than "what". If the PR
fixes an issue, add a 'Fixes: #<issue_number>' line. Keep line lengths of the
commit messages to 72 characters.

## Architecture Overview

### Core Components

**I/O Core (`src/iocore/`):**
- `eventsystem/` - Event-driven engine (Continuations, Events, Processors)
- `cache/` - Disk and RAM cache implementation
- `net/` - Network layer with TLS and QUIC support
- `dns/` - Asynchronous DNS resolution
- `hostdb/` - Internal DNS cache
- `aio/` - Asynchronous I/O

**Proxy Logic (`src/proxy/`):**
- `http/` - HTTP/1.1 protocol implementation
- `http2/` - HTTP/2 support
- `http3/` - HTTP/3 implementation
- `hdrs/` - Header parsing and management
- `logging/` - Flexible logging system
- `http/remap/` - URL remapping and routing

**Management (`src/mgmt/`):**
- JSONRPC server for runtime configuration and management
- Configuration file parsing and validation

**Base Libraries (`src/`):**
- `tscore/` - Core utilities and data structures
- `tsutil/` - Core utilities (metrics, debugging, regex, etc.)
- `api/` - Plugin API implementation
- `tscpp/api/` - C++ API wrapper for plugins
- `cripts/` - Cripts scripting framework for plugins

### Event-Driven Architecture

ATS uses a **Continuation-based** programming model:
- All async operations use Continuations (callback objects)
- Events are dispatched through an event system
- Multiple thread pools handle different event types
- Non-blocking I/O throughout

### Configuration Files

Primary configs (installed to `/etc/trafficserver/` by default):
- `records.yaml` - Main configuration (formerly records.config)
- `remap.config` - URL remapping rules
- `plugin.config` - Plugin loading configuration
- `ip_allow.yaml` - IP access control
- `ssl_multicert.config` - TLS certificate configuration
- `sni.yaml` - SNI-based routing
- `storage.config` - Cache storage configuration

Configuration can be modified at runtime via:
- `traffic_ctl config reload` (for some settings)
- JSONRPC API

## Plugin Development

### Plugin Types
- **Global plugins** - Loaded in `plugin.config`, affect all requests
- **Remap plugins** - Loaded per remap rule, affect specific mapped requests

### Key Plugin APIs
Headers in `include/ts/`:
- `ts.h` - Main plugin API
- `remap.h` - Remap plugin API
- `InkAPIPrivateIOCore.h` - Advanced internal APIs

### Plugin Hooks
Plugins register callbacks at various points in request processing:
- `TS_HTTP_READ_REQUEST_HDR_HOOK`
- `TS_HTTP_SEND_REQUEST_HDR_HOOK`
- `TS_HTTP_READ_RESPONSE_HDR_HOOK`
- `TS_HTTP_SEND_RESPONSE_HDR_HOOK`
- Many more (see `ts.h`)

### Example Plugins
- `example/plugins/` - Simple example plugins
- `plugins/` - Stable core plugins
- `plugins/experimental/` - Experimental plugins

## Common Development Patterns

### When Modifying HTTP Processing
1. Understand the state machine: `src/proxy/http/HttpSM.cc` - HTTP State Machine
2. Hook into the appropriate stage via plugin hooks or core modification
3. Use `HttpTxn` objects to access transaction state
4. Headers are accessed via `HDRHandle` and field APIs

### When Adding Configuration
1. Define record in `src/records/RecordsConfig.cc`
2. Add validation if needed
3. Update documentation in `doc/admin-guide/`
4. Configuration can be read via `REC_` APIs

### When Working with Cache
- Cache is content-addressable, keyed by URL (modifiable via plugins)
- Cache operations are async, continuation-based
- Cache has RAM and disk tiers
- Cache code is in `src/iocore/cache/`

### When Debugging
1. Enable debug tags in `records.yaml`:
   ```yaml
   proxy.config.diags.debug.enabled: 1
   proxy.config.diags.debug.tags: http|cache
   ```
2. Check logs in `/var/log/trafficserver/`:
   - `diags.log` - Debug output
   - `error.log` - Errors and warnings
3. Use `traffic_top` for live statistics
4. Use `traffic_ctl` for runtime inspection

**Debug Controls in Code:**
```cpp
static DbgCtl dbg_ctl{"my_component"};
SMDebug(dbg_ctl, "Processing request for URL: %s", url);
```

## Important Conventions

### License Headers
- Always add Apache License 2.0 headers to the top of new source and test files
- This includes `.cc`, `.h`, `.py`, and other code files
- Follow the existing license header format used in the codebase

### Code Style
- C++20 standard (nothing from C++23 or later)
- Use RAII principles
- Prefer smart pointers for ownership
- Don't use templates unless needed and appropriate
- Run `cmake --build build -t format` before committing
- Line length: 132 characters maximum
- Don't add comments where the code documents itself, don't comment claude interactions

**C++ Formatting (Mozilla-based style):**
- Indentation: 2 spaces for C/C++
- Braces: Linux style (opening brace on same line)
- Pointer alignment: Right (`Type *ptr`, not `Type* ptr`)
- Variable declarations: Add empty line after declarations before subsequent code
- Avoid naked conditions (always use braces with if statements)

**Naming Conventions:**
- CamelCase for classes: `HttpSM`, `NetVConnection`
- snake_case for variables and functions: `server_entry`, `handle_api_return()`
- UPPER_CASE for macros and constants: `HTTP_SM_SET_DEFAULT_HANDLER`

**Modern C++ Patterns (Preferred):**
```cpp
// GOOD - Modern C++20
auto buffer = std::make_unique<MIOBuffer>(alloc_index);
for (const auto &entry : container) {
  if (auto *vc = entry.get_vc(); vc != nullptr) {
    // Process vc
  }
}

// AVOID - Legacy C-style
MIOBuffer *buffer = (MIOBuffer*)malloc(sizeof(MIOBuffer));
```

### Python Code Style (for tests and tools)
- Python 3.11+ with proper type annotations
- 4-space indentation, never TABs

### Memory Management
- Custom allocators supported (jemalloc, mimalloc)
- Use `ats_malloc` family for large allocations
- IOBuffers for network data (zero-copy where possible)

### Threading
- Event threads handle most work
- DNS has dedicated threads
- Disk I/O uses thread pool
- Most code should be async/event-driven, not thread-based

### Error Handling
- Return error codes for recoverable errors
- Use `ink_release_assert` for unrecoverable errors
- Log errors appropriately (ERROR vs WARNING vs NOTE)

## Key Files for Understanding Architecture

- `include/iocore/eventsystem/Continuation.h` - Core async pattern
- `src/proxy/http/HttpSM.cc` - HTTP request state machine
- `src/iocore/net/UnixNetVConnection.cc` - Network connection handling
- `src/iocore/cache/Cache.cc` - Cache implementation
- `src/proxy/http/remap/RemapConfig.cc` - URL remapping logic
- `include/ts/ts.h` - Plugin API

## Resources

- Official docs: https://trafficserver.apache.org/
- Developer wiki: https://cwiki.apache.org/confluence/display/TS/
- CI dashboard: https://ci.trafficserver.apache.org/
- AuTest framework: https://autestsuite.bitbucket.io/
- Proxy Verifier: https://github.com/yahoo/proxy-verifier
