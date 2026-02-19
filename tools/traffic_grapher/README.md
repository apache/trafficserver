<!--
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
-->

# Traffic Grapher

Real-time ATS metrics visualization for iTerm2. Displays live graphs of
requests/sec, latency, cache hit rate, connections, and more — inline in
your terminal using imgcat.

## Features

- Real-time graphs of RPS, latency, cache hit rate, connections
- Support for 1–4 hosts with different line styles for comparison
- Collects metrics via JSONRPC Unix socket (batch collection per host)
- Dark theme optimized for terminal display
- Keyboard navigation between 4 metric pages
- Configurable refresh interval and history window
- Optional GUI mode via matplotlib window

## Requirements

- Python 3.9+
- [uv](https://docs.astral.sh/uv/) (recommended) or pip
- iTerm2 (or compatible terminal for inline images)
- SSH access to remote ATS hosts

## Quick Start

```bash
# With uv (handles dependencies automatically)
uv run traffic_grapher.py ats-server1.example.com

# Multiple hosts for comparison
uv run traffic_grapher.py ats-server{1..4}.example.com

# Custom interval and history
uv run traffic_grapher.py --interval 2 --history 120 ats-server1.example.com
```

## Installation (Alternative)

```bash
# Install as a project
uv sync
uv run traffic-grapher ats-server1.example.com

# Or with pip
pip install .
traffic-grapher ats-server1.example.com
```

## Configuration

### ATS Paths

The tool needs to know where the JSONRPC socket is on the remote host.
Configure via CLI flag or environment variable:

| Option | Env Var | Default |
|--------|---------|---------|
| `--socket` | `TRAFFICSERVER_JSONRPC_SOCKET` | `/usr/local/var/trafficserver/jsonrpc20.sock` |

### Custom Dashboards

Create a YAML config file to customize which metrics are displayed:

```bash
uv run traffic_grapher.py -c my_dashboard.yaml ats-server1.example.com
```

## Keyboard Controls

| Key | Action |
|-----|--------|
| `h` / `←` | Previous page |
| `l` / `→` | Next page |
| `q` | Quit |

## Pages

1. **Traffic & Cache** — Requests/sec, latency, cache hit rate, connections
2. **Response Codes** — 2xx, 3xx, 4xx, 5xx breakdown
3. **TLS & HTTP/2** — SSL handshakes, connections, HTTP/2 stats
4. **Network & Errors** — Bandwidth, connection errors, transaction errors

## Options

```
--interval SEC     Refresh interval in seconds (default: 1.0)
--history SEC      History window in seconds (default: 60)
--socket PATH      Path to JSONRPC Unix socket on remote host
--gui              Use matplotlib GUI window instead of imgcat
--once             Single snapshot, then exit
--timezone TZ      Timezone for display (default: UTC)
--save-png FILE    Save PNG after each render (use {iter} for iteration)
--no-keyboard      Disable keyboard handling (for non-TTY environments)
```
