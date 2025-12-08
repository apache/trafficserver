# filter_body Remap Plugin Implementation

## Overview

Create a YAML-configured remap plugin to detect and block CVE exploitation attempts by:
1. Matching case-insensitive patterns in request OR response headers
2. **Zero-copy** streaming body inspection with minimal lookback buffer
3. Configurable actions per rule: log, block, and/or add header
4. Per-rule direction: inspect request (to origin) or response (from origin)

## Key Files to Create

- `plugins/experimental/filter_body/filter_body.cc` - Main plugin
- `plugins/experimental/filter_body/readme.txt` - Documentation
- `tests/gold_tests/pluginTest/filter_body/filter_body.test.py` - AuTest
- `tests/gold_tests/pluginTest/filter_body/replay/*.replay.yaml` - Replay files
- `tests/gold_tests/pluginTest/filter_body/filter_body.yaml` - Plugin config

## Configuration (YAML)

```yaml
rules:
  # REQUEST rule - block malicious requests to origin
  - name: "xxe_detection"
    direction: request
    action: [log, block, add_header]
    add_header:
      name: "@filter-match"
      value: "xxe-blocked"
    methods: [POST]
    max_content_length: 1048576
    headers:
      - name: "Content-Type"
        patterns: ["application/xml", "text/xml"]
    body_patterns: ["<!ENTITY", "SYSTEM"]

  # RESPONSE rule - filter sensitive data from origin
  - name: "ssn_leak_detection"
    direction: response
    action: [log, add_header]
    add_header:
      name: "X-Data-Leak"
      value: "ssn-detected"
    methods: [GET, POST]
    headers:
      - name: "Content-Type"
        patterns: ["application/json"]
    body_patterns: ["\\d{3}-\\d{2}-\\d{4}"]
```

**Direction:** `request` (default) or `response`

**Actions (array, default: [log]):** `log`, `block`, `add_header`

## Zero-Copy Streaming Design

**Key principle:** No data copying during normal passthrough. Only copy into minimal lookback buffer.

```cpp
// Zero-copy: read directly from buffer blocks
TSIOBufferBlock block = TSIOBufferReaderStart(reader);
const char *data = TSIOBufferBlockReadStart(block, reader, &avail);
// Search 'data' directly - no memcpy

// Lookback buffer: only (max_pattern_len - 1) bytes
// Copy only tail bytes needed for cross-boundary detection
```

**Implementation:**
1. `TSIOBufferBlockReadStart` - Get direct pointer to buffer data (no copy)
2. Search pattern directly in buffer block memory
3. For boundary handling: copy only last `(max_pattern_len - 1)` bytes to small lookback
4. `TSIOBufferCopy` - Pass through data without touching it (zero-copy passthrough)

```
[tiny lookback][-------- buffer block (direct pointer) --------]
     ^-- only this small piece is copied for boundary handling
```

## Matching Logic

1. Evaluate rules based on `direction` (request/response)
2. For body inspection to trigger:
   - Method in `methods` list
   - Content-Length <= `max_content_length` (if set)
   - ALL headers match (AND); within each, ANY pattern matches (OR, case-insensitive)
3. Stream through body zero-copy, search patterns in-place
4. On match, execute actions: `log`, `add_header`, `block`

## Key API Calls

- `TS_HTTP_REQUEST_TRANSFORM_HOOK` / `TS_HTTP_RESPONSE_TRANSFORM_HOOK`
- `TSIOBufferBlockReadStart` - Zero-copy read pointer
- `TSIOBufferCopy` - Zero-copy passthrough
- `TSMimeHdrFieldCreate` / `TSMimeHdrFieldAppend` - Add header
- `TSHttpTxnStatusSet` + `TSHttpTxnReenable(TS_EVENT_HTTP_ERROR)` - Block

## Build Integration

Add to `cmake/ExperimentalPlugins.cmake`:
```cmake
auto_option(FILTER_BODY FEATURE_VAR BUILD_FILTER_BODY DEFAULT ${_DEFAULT})
```

Add to `plugins/experimental/CMakeLists.txt`:
```cmake
if(BUILD_FILTER_BODY)
  add_subdirectory(filter_body)
endif()
```

Create `plugins/experimental/filter_body/CMakeLists.txt`:
```cmake
project(filter_body)
add_atsplugin(filter_body filter_body.cc)
target_link_libraries(filter_body PRIVATE yaml-cpp::yaml-cpp)
verify_remap_plugin(filter_body)
```

## AuTest (ATSReplayTest)

Files:
- `filter_body.test.py` - Test driver using ATSReplayTest
- `replay/block_request.replay.yaml` - Request block test
- `replay/log_only.replay.yaml` - Log only, pass through
- `replay/add_header.replay.yaml` - Header addition test
- `replay/response_filter.replay.yaml` - Response body filtering
- `filter_body.yaml` - Plugin configuration

Test cases:
1. Request match + action:[block] -> 403
2. Request match + action:[log] -> 200, passes
3. Request match + action:[add_header] -> header added
4. Response match + block -> 403 to client
5. Content-Length > max -> skip inspection
6. Partial header match -> no body inspection

