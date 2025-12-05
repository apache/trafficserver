filter_body - Request/Response Body Content Filter Plugin
=========================================================

Overview
--------
The filter_body plugin is a remap plugin that performs zero-copy streaming
inspection of request or response bodies to detect CVE exploitation attempts
and other malicious patterns. When configured patterns are matched, the plugin
can log, block (return 403), and/or add headers.

Features
--------
- Zero-copy streaming body inspection (no full buffering)
- Case-insensitive header pattern matching
- Case-sensitive body pattern matching
- Handles patterns that span buffer boundaries
- Per-rule direction: inspect request or response
- Configurable actions: log, block, add_header
- Optional Content-Length limit to skip large payloads

Configuration
-------------
The plugin uses a YAML configuration file. Usage in remap.config:

  map http://example.com/ http://origin.com/ @plugin=filter_body.so @pparam=filter_body.yaml

Example filter_body.yaml:

  rules:
    # Block XXE attacks in XML requests
    - name: "xxe_detection"
      direction: request              # "request" (default) or "response"
      action: [log, block]            # Actions to take on match
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

    # Detect and tag suspicious API requests
    - name: "proto_pollution"
      direction: request
      action: [log, add_header]       # Log and add header, but don't block
      add_header:
        name: "X-Security-Match"
        value: "proto-pollution"
      methods: [POST, PUT]
      headers:
        - name: "Content-Type"
          patterns: ["application/json"]
        - name: "User-Agent"          # ALL headers must match (AND)
          patterns: ["curl", "python"]
      body_patterns:
        - "__proto__"
        - "constructor"

    # Filter sensitive data from responses
    - name: "ssn_leak"
      direction: response
      action: [log, block]
      methods: [GET, POST]
      headers:
        - name: "Content-Type"
          patterns: ["application/json", "text/html"]
      body_patterns:
        - "SSN:"
        - "social security"

Configuration Fields
--------------------
rules:              Array of filter rules

Per-rule fields:
  name:             Rule name (required, used in logging)
  direction:        "request" or "response" (default: request)
  action:           Array of actions (default: [log])
                    - "log": Log match to error.log
                    - "block": Return 403 Forbidden
                    - "add_header": Add configured header
  add_header:       Header to add when "add_header" action is used
    name:           Header name (can start with @ for internal headers)
    value:          Header value
  methods:          Array of HTTP methods to inspect (empty = all)
  max_content_length: Skip inspection if Content-Length exceeds this
  headers:          Array of header conditions (ALL must match)
    name:           Header name to check
    patterns:       Patterns to search for (ANY matches, case-insensitive)
  body_patterns:    Array of body patterns (ANY matches, case-sensitive)

Matching Logic
--------------
1. Rules are evaluated based on direction (request/response)
2. For body inspection to trigger:
   - Method must match (if configured)
   - Content-Length must be <= max_content_length (if configured)
   - ALL header conditions must match
   - Within each header, ANY pattern matches (OR, case-insensitive)
3. Body is streamed through and searched for patterns (case-sensitive)
4. If ANY body pattern matches, configured actions are executed

Performance Notes
-----------------
- Uses zero-copy streaming; data is not buffered entirely
- Only a small lookback buffer (max_pattern_length - 1 bytes) is maintained
  to detect patterns that span buffer boundaries
- Use max_content_length to skip inspection of large payloads
- Header matching is done before any body processing begins

Building
--------
The plugin requires yaml-cpp. Enable with:

  cmake -DBUILD_FILTER_BODY=ON ...

Or build all experimental plugins:

  cmake -DBUILD_EXPERIMENTAL_PLUGINS=ON ...

License
-------
Licensed to the Apache Software Foundation (ASF) under the Apache License,
Version 2.0.

