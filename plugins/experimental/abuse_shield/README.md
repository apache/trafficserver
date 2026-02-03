# Abuse Shield Plugin

A unified abuse protection plugin for Apache Traffic Server that provides HTTP/2 error tracking and IP-based abuse detection using the Udi "King of the Hill" algorithm.

## Overview

The Abuse Shield plugin protects against HTTP/2 protocol attacks and other abuse patterns by:

1. **Tracking HTTP/2 errors** - Monitors all 16 HTTP/2 error codes per IP, distinguishing between client-caused and server-caused errors
2. **Detecting "pure attacks"** - Identifies IPs with many errors but zero successful requests
3. **Rate limiting** - Tracks connection and request rates per IP
4. **Bounded memory** - Uses the Udi algorithm for efficient, fixed-memory IP tracking
5. **Dynamic configuration** - Supports runtime config reload via `traffic_ctl plugin msg`

## Algorithm

The plugin uses the "Udi King of the Hill" algorithm (based on Yahoo's CARP patent 7533414) for IP tracking:

- Fixed-size slot array (configurable, default 50K slots)
- New IPs contest existing slots based on score
- High-score IPs naturally remain in the table
- Low-activity IPs are automatically evicted
- Memory is bounded regardless of traffic volume

## Installation

Build with CMake:

```bash
cmake .. -DENABLE_ABUSE_SHIELD=ON
cmake --build . --target abuse_shield
```

Add to `plugin.config`:

```
abuse_shield.so abuse_shield.yaml
```

## Configuration

Create `abuse_shield.yaml` in the config directory:

```yaml
ip_reputation:
  slots: 50000              # Number of IP tracking slots
  window_seconds: 60        # Time window for rate calculations

blocking:
  duration_seconds: 300     # Block duration (5 minutes)

trusted_ips_file: /etc/trafficserver/abuse_shield_trusted.txt

rules:
  # Block HTTP/2 protocol errors
  - name: "protocol_error_flood"
    filter:
      h2_error: 0x01        # PROTOCOL_ERROR
      min_count: 10
    action: [log, block, close]

  # Detect pure attacks (errors with no successes)
  - name: "pure_attack"
    filter:
      min_client_errors: 10
      max_successes: 0
    action: [log, block, close]

enabled: true
```

## Rule Filters

| Filter | Description |
|--------|-------------|
| `h2_error` | Specific HTTP/2 error code (0x00-0x0f) |
| `min_count` | Minimum count for `h2_error` |
| `min_client_errors` | Total client-caused errors |
| `min_server_errors` | Total server-caused errors |
| `max_successes` | Maximum successful requests (use 0 for pure attacks) |
| `max_conn_rate` | Max connections per window |
| `max_req_rate` | Max requests per window |

## Actions

| Action | Description |
|--------|-------------|
| `log` | Log to error.log with all tracked attributes |
| `block` | Block the IP for `blocking.duration_seconds` |
| `close` | Immediately close the connection |
| `downgrade` | Downgrade to HTTP/1.1 (Phase 2) |

## HTTP/2 Error Codes

| Code | Name | Typical Cause | CVEs |
|------|------|---------------|------|
| 0x01 | PROTOCOL_ERROR | Client | CVE-2019-9513, CVE-2019-9518 |
| 0x02 | INTERNAL_ERROR | Server | |
| 0x03 | FLOW_CONTROL_ERROR | Client | CVE-2019-9511, CVE-2019-9517 |
| 0x04 | SETTINGS_TIMEOUT | Client | |
| 0x05 | STREAM_CLOSED | Client | |
| 0x06 | FRAME_SIZE_ERROR | Client | |
| 0x07 | REFUSED_STREAM | Server | |
| 0x08 | CANCEL | Client | CVE-2023-44487 (Rapid Reset) |
| 0x09 | COMPRESSION_ERROR | Client | CVE-2016-1544 (HPACK bomb) |
| 0x0a | CONNECT_ERROR | Either | |
| 0x0b | ENHANCE_YOUR_CALM | Server | |
| 0x0c | INADEQUATE_SECURITY | Either | |
| 0x0d | HTTP_1_1_REQUIRED | Server | |

## Dynamic Control

Reload configuration:
```bash
traffic_ctl plugin msg abuse_shield.reload
```

Dump tracked IPs:
```bash
traffic_ctl plugin msg abuse_shield.dump
```

Reset table metrics (contests, evictions) without removing tracked IPs:
```bash
traffic_ctl plugin msg abuse_shield.reset
```

Enable/disable:
```bash
traffic_ctl plugin msg abuse_shield.enabled 1
traffic_ctl plugin msg abuse_shield.enabled 0
```

## Trusted IPs

Create `abuse_shield_trusted.txt` with one IP or CIDR per line:

```
# Localhost
127.0.0.1
::1

# Internal networks
10.0.0.0/8
192.168.0.0/16
```

## Metrics

View metrics with `traffic_ctl metric get abuse_shield.*`:

| Metric | Description |
|--------|-------------|
| `abuse_shield.rules.matched` | Total times any rule filter condition was true |
| `abuse_shield.actions.blocked` | Total times block action executed |
| `abuse_shield.actions.closed` | Total times close action executed |
| `abuse_shield.actions.logged` | Total times log action executed |
| `abuse_shield.connections.rejected` | Connections rejected from previously blocked IPs |

## Memory Usage

Memory is bounded by `slots` configuration:

| Slots | Memory |
|-------|--------|
| 10,000 | ~1.6 MB |
| 50,000 | ~8.0 MB |
| 100,000 | ~16.0 MB |

Each slot is approximately 128 bytes.

## Comparison with block_errors

| Feature | abuse_shield | block_errors |
|---------|--------------|--------------|
| H2 error codes | All 16 | Only 2 (CANCEL, ENHANCE_YOUR_CALM) |
| Client vs server errors | Yes | No |
| Memory bounded | Yes (Udi) | No (unbounded hash map) |
| YAML config | Yes | No (command line) |
| Dynamic reload | Yes | Partial |
| Per-IP successes | Yes | No |
| Pure attack detection | Yes | No |

## License

Licensed under the Apache License, Version 2.0.
