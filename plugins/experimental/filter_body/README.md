# filter_body - Request/Response Body Content Filter Plugin

## Overview

The `filter_body` plugin is a remap plugin that performs zero-copy streaming
inspection of request or response bodies to detect CVE exploitation attempts
and other malicious patterns. When configured patterns are matched, the plugin
can log, block (return 403), and/or add headers.

## Features

- Zero-copy streaming body inspection (no full buffering).
- Case-insensitive header pattern matching.
- Case-sensitive body pattern matching.
- Handles patterns that span buffer boundaries.
- Per-rule direction: inspect request or response.
- Configurable actions: log, block, add_header.
- Optional Content-Length limit to skip large payloads.
- Per-rule metrics counters.

## Configuration

The plugin uses a YAML configuration file. Usage in `remap.config`:

```
map http://example.com/ http://origin.com/ @plugin=filter_body.so @pparam=filter_body.yaml
```

### Example Configuration

The configuration uses a `filter` node to group all filtering criteria,
keeping them separate from the `action`:

```yaml
rules:
  # Block XXE attacks in XML requests.
  - name: "xxe_detection"
    filter:
      direction: request              # "request" (default) or "response"
      methods: [POST]                 # HTTP methods to inspect
      max_content_length: 1048576     # Skip bodies larger than 1MB
      headers:
        - name: "Content-Type"
          patterns:                   # Case-insensitive, ANY matches (OR)
            - "application/xml"
            - "text/xml"
      body_patterns:                  # Case-sensitive, ANY matches
        - "<!ENTITY"
        - "SYSTEM"
    action:
      - log
      - block

  # Detect and tag suspicious API requests.
  - name: "proto_pollution"
    filter:
      direction: request
      methods: [POST, PUT]
      headers:
        - name: "Content-Type"
          patterns: ["application/json"]
        - name: "User-Agent"          # ALL headers must match (AND)
          patterns: ["curl", "python"]
      body_patterns:
        - "__proto__"
        - "constructor"
    action:
      - log
      - add_header:
          X-Security-Match: "<rule_name>"
          X-Threat-Type: "proto-pollution"

  # Filter sensitive data from responses.
  - name: "ssn_leak"
    filter:
      direction: response
      status: [200]                   # Only inspect 200 responses
      headers:
        - name: "Content-Type"
          patterns: ["application/json", "text/html"]
      body_patterns:
        - "SSN:"
        - "social security"
    action:
      - log
      - block
```

## Configuration Fields

### Top Level

| Field | Description |
|-------|-------------|
| `rules` | Array of filter rules. |

### Per-Rule Fields

| Field | Description |
|-------|-------------|
| `name` | Rule name (required, used in logging and metrics). |
| `filter` | Container for all filtering criteria (required). |
| `action` | Array of actions (default: `[log]`). |

### Filter Section Fields

| Field | Description |
|-------|-------------|
| `direction` | `"request"` or `"response"` (default: `request`). |
| `methods` | Array of HTTP methods to inspect (empty = all, request rules only). |
| `status` | Array of HTTP status codes to match (response rules only). |
| `max_content_length` | Skip inspection if Content-Length exceeds this value. |
| `headers` | Array of header conditions (ALL must match). |
| `body_patterns` | Array of body patterns to search for (ANY matches). |

### Actions

- `log` - Log match to `diags.log`.
- `block` - Return 403 Forbidden.
- `add_header` - Add configured headers (supports multiple headers and `<rule_name>` substitution).

```yaml
action:
  - log
  - add_header:
      X-Security-Match: "<rule_name>"
      X-Another-Header: "some-value"
```

The `<rule_name>` placeholder is replaced with the rule's `name` value at
runtime.

### Header Conditions

```yaml
filter:
  headers:
    - name: "Content-Type"      # Header name (case-insensitive)
      patterns:                 # Patterns to match (OR logic, case-insensitive)
        - "application/xml"
        - "text/xml"
```

## Matching Logic

1. Rules are evaluated based on direction (request/response).
2. For body inspection to trigger:
   - Method must match (if configured, request rules only).
   - Status code must match (if configured, response rules only).
   - Content-Length must be ≤ `max_content_length` (if configured).
   - ALL header conditions must match.
   - Within each header, ANY pattern matches (OR, case-insensitive).
3. Body is streamed through and searched for patterns (case-sensitive).
4. If ANY body pattern matches, configured actions are executed.

## Performance Notes

- Uses zero-copy streaming; data is not buffered entirely.
- Only a small lookback buffer (`max_pattern_length - 1` bytes) is maintained
  to detect patterns that span buffer boundaries.
- Use `max_content_length` to skip inspection of large payloads.
- Header matching is done before any body processing begins.

## Metrics

The plugin creates a metrics counter for each rule:

```
plugin.filter_body.rule.<rule_name>.matches
```

Query with `traffic_ctl`:

```bash
traffic_ctl metric get plugin.filter_body.rule.xxe_detection.matches
traffic_ctl metric match plugin.filter_body
```

## Building

Enable with cmake:

```bash
cmake -DENABLE_FILTER_BODY=ON ...
```

Or build all experimental plugins:

```bash
cmake -DBUILD_EXPERIMENTAL_PLUGINS=ON ...
```

## Documentation

For comprehensive documentation, see the [Admin Guide](../../../doc/admin-guide/plugins/filter_body.en.rst).

## License

Licensed to the Apache Software Foundation (ASF) under the Apache License, Version 2.0.
