# Apache Traffic Server - GitHub Copilot Instructions

## Repository Overview

Apache Traffic Server (ATS) is a high-performance HTTP/HTTPS caching proxy server written in C++20. It processes large-scale web traffic using an event-driven, multi-threaded architecture with a sophisticated plugin system.

**Key Facts:**
- ~500K lines of C++20 code (strict - no C++23)
- Event-driven architecture using Continuation callbacks
- Handles HTTP/1.1, HTTP/2, HTTP/3 (QUIC)
- Supports TLS termination and caching
- Extensible via C/C++ plugins

## Code Style Requirements

### C++ Style Guidelines

Some of these rules are enforced automatically by CI (via clang-format and clang-tidy); others are recommended conventions that will not themselves cause CI failures but should still be followed in code reviews.
**Formatting Rules:**
- **Line length:** 132 characters maximum
- **Indentation:** 2 spaces (never tabs)
- **Braces:** Linux kernel style (opening brace on same line)
```cpp
if (condition) {
  // code here
}
```
- **Pointer/reference alignment:** Right side
```cpp
Type *ptr;        // Correct
Type &ref;        // Correct
Type* ptr;        // Wrong
```
- **Empty line after declarations:**
```cpp
void function() {
  int x = 5;
  std::string name = "test";

  // Empty line before first code statement
  process_data(x, name);
}
```
- **Always use braces** for if/while/for (no naked conditions):
```cpp
if (x > 0) {    // Correct
  foo();
}

if (x > 0)      // Wrong - missing braces
  foo();
```
- **Keep variable declarations together:**
```cpp
// Correct - declarations grouped together
void function() {
  int count = 0;
  std::string name = "test";
  auto *buffer = new_buffer();

  // Empty line before first code statement
  process_data(count, name, buffer);
}

// Wrong - declarations scattered
void function() {
  int count = 0;
  process_count(count);
  std::string name = "test";  // Don't scatter declarations
}
```

**Naming Conventions:**
- Classes: `CamelCase` → `HttpSM`, `NetVConnection`, `CacheProcessor`
- Functions/variables: `snake_case` → `handle_request()`, `server_port`, `cache_key`
- Constants/macros: `UPPER_CASE` → `HTTP_STATUS_OK`, `MAX_BUFFER_SIZE`
- Member variables: `snake_case` with no prefix → `connection_count`, `buffer_size`

**C++20 Patterns (Use These):**
```cpp
// GOOD - Modern C++20
auto buffer = std::make_unique<MIOBuffer>(size);
for (const auto &entry : container) {
  if (auto *ptr = entry.get(); ptr != nullptr) {
    process(ptr);
  }
}

// AVOID - Legacy C-style
MIOBuffer *buffer = (MIOBuffer*)malloc(sizeof(MIOBuffer));
for (int i = 0; i < container.size(); i++) {
  process(container[i]);
}
```

**Memory Management:**
- Use RAII and smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Use `ats_malloc()`/`ats_free()` for large allocations (not `malloc`)
- Use `IOBuffer` for network data (zero-copy design)
- Prefer RAII/smart pointers over manual `delete` where practical; some subsystems legitimately use explicit deletes / `delete this`

**What NOT to Use:**
- ❌ C++23 features (code must compile with C++20)
- ❌ Raw `new`/`delete` (use smart pointers)
- ❌ `malloc`/`free` for large allocations (use `ats_malloc`)
- ❌ Blocking operations in event threads
- ❌ Creating threads manually (use async event system)

### Comments and Documentation

**Minimal comments philosophy:**
- Only add comments where code isn't self-explanatory
- **Don't** describe what the code does (the code already shows that)
- **Do** explain *why* something is done if not obvious
- Avoid stating the obvious

```cpp
// BAD - stating the obvious
// Increment the counter
counter++;

// GOOD - explaining why
// Skip the first element since it's always the sentinel value
counter++;

// BAD - describing what
// Loop through all connections and close them
for (auto &conn : connections) {
  conn.close();
}

// GOOD - explaining why (if not obvious)
// Must close connections before destroying the acceptor to avoid use-after-free
for (auto &conn : connections) {
  conn.close();
}
```

**When to add comments:**
- Non-obvious algorithms or math
- Workarounds for bugs in dependencies
- Performance optimizations that reduce clarity
- Security-critical sections
- Complex state machine transitions

**When NOT to add comments:**
- Self-documenting code
- Obvious operations
- Function/variable names that explain themselves

### Python Style

- Python 3.11+ with type hints
- 4-space indentation (never tabs)
- Type annotations on all function signatures

### License Headers

**New source and test files** must start with Apache License 2.0 header (`.cc`, `.h`, `.py`, and other code files):
```cpp
/** @file
 *
 *  Brief description of file
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
```

## Architecture Patterns

### Event-Driven Model (CRITICAL)

ATS uses **Continuation-based** asynchronous programming:

```cpp
// Continuation is the core callback pattern
class MyContinuation : public Continuation {
  int handle_event(int event, void *data) {
    switch (event) {
    case EVENT_SUCCESS:
      // Handle success
      return EVENT_DONE;
    case EVENT_ERROR:
      // Handle error
      return EVENT_ERROR;
    }
    return EVENT_CONT;
  }
};
```

**Key Rules:**
- ⚠️ **NEVER block in event threads** - use async I/O or thread pools
- All async operations use `Continuation` callbacks
- Return `EVENT_DONE`, `EVENT_CONT`, or `EVENT_ERROR` from handlers
- Use `EThread::schedule()` for deferred work

### HTTP State Machine

The `HttpSM` class orchestrates HTTP request processing:

```cpp
// HttpSM is the central state machine
class HttpSM : public Continuation {
  // Processes requests through various states
  // Hook into appropriate stage via plugin hooks
  // Access transaction state via HttpTxn
};
```

**Common hooks:**
- `TS_HTTP_READ_REQUEST_HDR_HOOK` - After reading client request
- `TS_HTTP_SEND_REQUEST_HDR_HOOK` - Before sending to origin
- `TS_HTTP_READ_RESPONSE_HDR_HOOK` - After reading origin response
- `TS_HTTP_SEND_RESPONSE_HDR_HOOK` - Before sending to client

### Threading Model

- **Event threads:** Handle most async work (never block here)
- **DNS threads:** Dedicated DNS resolution pool
- **Disk I/O threads:** Cache disk operations
- **Network threads:** Actually event threads handling network I/O

**Rule:** Don't create threads. Use the event system or existing thread pools.

### Debug Logging Pattern

```cpp
// At file scope
static DbgCtl dbg_ctl{"http_sm"};

// In code
DbgPrint(dbg_ctl, "Processing request for URL: %s", url);
```

## Project Structure (Key Paths)

```
trafficserver/
├── src/
│   ├── iocore/              # I/O subsystem
│   │   ├── eventsystem/    # Event engine (Continuation.h is core)
│   │   ├── cache/          # Cache implementation
│   │   ├── net/            # Network I/O, TLS, QUIC
│   │   └── dns/            # DNS resolution
│   ├── proxy/              # HTTP proxy logic
│   │   ├── http/           # HTTP/1.1 (HttpSM.cc is central)
│   │   ├── http2/          # HTTP/2
│   │   ├── http3/          # HTTP/3
│   │   ├── hdrs/           # Header parsing
│   │   └── logging/        # Logging
│   ├── tscore/             # Core utilities
│   ├── tsutil/             # Utilities (metrics, debugging)
│   └── api/                # Plugin API implementation
│
├── include/
│   ├── ts/                 # Public plugin API (ts.h)
│   ├── tscpp/              # C++ plugin API
│   └── iocore/             # Internal headers
│
├── plugins/                # Stable plugins
│   ├── header_rewrite/    # Header manipulation (see HRW.instructions.md)
│   └── experimental/      # Experimental plugins
│
└── tools/
    └── hrw4u/             # Header Rewrite DSL compiler

```

### Key Files to Understand

- `include/iocore/eventsystem/Continuation.h` - Core async pattern
- `src/proxy/http/HttpSM.cc` - HTTP state machine (most important)
- `src/iocore/cache/Cache.cc` - Cache implementation
- `include/ts/ts.h` - Plugin API (most stable interface)
- `include/tscore/ink_memory.h` - Memory allocation functions

## Common Patterns

### Finding Examples

**Before writing new code, look for similar existing code:**
- Plugin examples: `example/plugins/` for simple patterns
- Stable plugins: `plugins/` for production patterns
- Experimental plugins: `plugins/experimental/` for newer approaches

**Pattern discovery:**
- Search for similar functionality in existing code
- Check `include/ts/ts.h` for plugin API patterns
- Look at tests in `tests/gold_tests/` for usage examples

### Code Organization

**Typical file structure for a plugin:**
```
plugins/my_plugin/
├── my_plugin.cc        # Main plugin logic
├── handler.cc          # Request/response handlers
├── handler.h           # Handler interface
├── config.cc           # Configuration parsing
└── CMakeLists.txt      # Build configuration
```

**Typical class structure:**
- Inherit from `Continuation` for async operations
- Implement `handle_event()` for event processing
- Store state in class members, not globals
- Clean up resources in destructor (RAII)

### Async Operation Pattern

**General structure for async operations:**
1. Create continuation with callback
2. Initiate async operation (returns `Action*`)
3. Handle callback events in `handle_event()`
4. Return `EVENT_DONE` when complete

**Always async, never blocking:**
- Network I/O → Use VConnection
- Cache operations → Use CacheProcessor
- DNS lookups → Use DNSProcessor
- Delayed work → Use `schedule_in()` or `schedule_at()`

### Error Handling

**Recoverable errors:**
- Return error codes
- Log with appropriate severity
- Clean up resources (RAII helps)

**Unrecoverable errors:**
- Use `ink_release_assert()` for conditions that should never happen
- Log detailed context before asserting

### Testing Approach

**When adding new functionality:**
1. Check if unit tests exist in same directory (Catch2)
2. Add integration tests in `tests/gold_tests/` (autest)
3. Prefer `Test.ATSReplayTest()` with `replay.yaml` format (Proxy Verifier)
4. Test both success and error paths

## Configuration

### Adding New Configuration Records

1. Define in `src/records/RecordsConfig.cc`:
```cpp
{RECT_CONFIG, "proxy.config.my_feature.enabled", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, nullptr, RECA_NULL}
```

2. Read in code:
```cpp
int enabled = 0;
REC_ReadConfigInteger(enabled, "proxy.config.my_feature.enabled");
```

## What to Avoid

### Common Mistakes

❌ **Blocking in event threads:**
```cpp
// WRONG - blocks event thread
sleep(5);
blocking_network_call();
```

✅ **Use async operations:**
```cpp
// CORRECT - schedules continuation
eventProcessor.schedule_in(this, HRTIME_SECONDS(5));
```

❌ **Manual memory management:**
```cpp
// WRONG
auto *obj = new MyObject();
// ... might leak if exception thrown
delete obj;
```

✅ **Use RAII:**
```cpp
// CORRECT
auto obj = std::make_unique<MyObject>();
// Automatically cleaned up
```

❌ **Creating threads:**
```cpp
// WRONG
std::thread t([](){ do_work(); });
```

✅ **Use event system:**
```cpp
// CORRECT
eventProcessor.schedule_imm(continuation, ET_CALL);
```

## Additional Resources

- Plugin API: `include/ts/ts.h`
- Event system: `include/iocore/eventsystem/`
- HTTP state machine: `src/proxy/http/HttpSM.cc`
- Documentation: `doc/developer-guide/`
