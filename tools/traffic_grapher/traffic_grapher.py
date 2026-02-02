#!/usr/bin/env python3
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""
Traffic Grapher - Real-time ATS metrics visualization.

Displays metrics inline in iTerm using imgcat with multi-host overlay comparison,
keyboard navigation between pages, and flicker-free live updates.

Usage:
    traffic_grapher.py ats-server1.example.com
    traffic_grapher.py ats-server{1..4}.example.com
    traffic_grapher.py --interval 2 --history 120 ats-server1.example.com ats-server2.example.com
"""

import argparse
import base64
import fcntl
import gc
import io
import os
import select
import shutil
import struct
import subprocess
import sys
import termios
import time
import tty
from collections import deque
from datetime import datetime, timezone
from typing import Optional, Tuple

try:
    from zoneinfo import ZoneInfo
except ImportError:
    from datetime import tzinfo
    ZoneInfo = None  # Will fall back to UTC only

import matplotlib
# Check for --gui in sys.argv early to set backend before importing pyplot
if '--gui' in sys.argv:
    # Use MacOSX on macOS, fall back to TkAgg on other platforms
    import platform
    if platform.system() == 'Darwin':
        matplotlib.use('MacOSX')
    else:
        matplotlib.use('TkAgg')
else:
    matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
import yaml

# =============================================================================
# Built-in Default Pages - 2x2 grid per page
# =============================================================================

DEFAULT_PAGES = [
    {
        "name": "Traffic & Cache",
        "panels":
            [
                {
                    "title": "Requests/sec",
                    "metrics":
                        [
                            {
                                "name": "Client",
                                "type": "counter",
                                "key": "proxy.process.http.incoming_requests",
                                "color": "#FFFF00"
                            },  # Yellow
                            {
                                "name": "Origin",
                                "type": "counter",
                                "key": "proxy.process.http.outgoing_requests",
                                "color": "#BF00FF"
                            },  # Electric Purple (complement)
                        ]
                },
                {
                    "title": "Latency (ms)",
                    "metrics":
                        [
                            {
                                "name": "Cache Hit",
                                "type": "latency",
                                "key": "proxy.process.http.transaction_totaltime.hit_fresh",
                                "key2": "proxy.process.http.transaction_counts.hit_fresh",
                                "color": "#00FF00"
                            },  # Lime Green
                            {
                                "name": "Origin",
                                "type": "latency",
                                "key": "proxy.process.http.transaction_totaltime.miss_cold",
                                "key2": "proxy.process.http.transaction_counts.miss_cold",
                                "color": "#FF00FF"
                            },  # Magenta (complement)
                        ]
                },
                {
                    "title": "Cache Hit Rate %",
                    "metrics":
                        [
                            {
                                "name": "Hit Rate",
                                "type": "hit_rate",
                                "key": "proxy.process.cache_total_hits",
                                "key2": "proxy.process.cache_total_misses",
                                "color": "#00FF7F"
                            },  # Spring Green
                        ]
                },
                {
                    "title": "Connections",
                    "metrics":
                        [
                            {
                                "name": "Client",
                                "type": "gauge",
                                "key": "proxy.process.http.current_client_connections",
                                "color": "#00FFFF"
                            },  # Cyan
                            {
                                "name": "Origin",
                                "type": "gauge",
                                "key": "proxy.process.http.current_server_connections",
                                "color": "#FF4040"
                            },  # Bright Red (complement)
                        ]
                },
            ]
    },
    {
        "name": "Response Codes",
        "panels":
            [
                {
                    "title": "2xx Responses/sec",
                    "metrics": [{
                        "name": "2xx",
                        "type": "counter",
                        "key": "proxy.process.http.2xx_responses",
                        "color": "#00FF00"
                    },]
                },
                {
                    "title": "3xx Responses/sec",
                    "metrics": [{
                        "name": "3xx",
                        "type": "counter",
                        "key": "proxy.process.http.3xx_responses",
                        "color": "#00FFFF"
                    },]
                },
                {
                    "title": "4xx Responses/sec",
                    "metrics": [{
                        "name": "4xx",
                        "type": "counter",
                        "key": "proxy.process.http.4xx_responses",
                        "color": "#FFA500"
                    },]
                },
                {
                    "title": "5xx Responses/sec",
                    "metrics": [{
                        "name": "5xx",
                        "type": "counter",
                        "key": "proxy.process.http.5xx_responses",
                        "color": "#FF0000"
                    },]
                },
            ]
    },
    {
        "name": "TLS & HTTP/2",
        "panels":
            [
                {
                    "title": "SSL Handshakes/sec",
                    "metrics":
                        [
                            {
                                "name": "Success",
                                "type": "counter",
                                "key": "proxy.process.ssl.total_success_handshake_count_in",
                                "color": "#00FF00"
                            },
                            {
                                "name": "Failed",
                                "type": "counter",
                                "key": "proxy.process.ssl.ssl_error_ssl",
                                "color": "#FF0000"
                            },
                        ]
                },
                {
                    "title": "SSL Connections",
                    "metrics":
                        [{
                            "name": "Active",
                            "type": "gauge",
                            "key": "proxy.process.ssl.user_agent_sessions",
                            "color": "#00FFFF"
                        },]
                },
                {
                    "title": "HTTP/2 Connections",
                    "metrics":
                        [
                            {
                                "name": "Total",
                                "type": "counter",
                                "key": "proxy.process.http2.total_client_connections",
                                "color": "#FF00FF"
                            },
                            {
                                "name": "Active",
                                "type": "gauge",
                                "key": "proxy.process.http2.current_client_sessions",
                                "color": "#00FFFF"
                            },
                        ]
                },
                {
                    "title": "HTTP/2 Errors/sec",
                    "metrics":
                        [
                            {
                                "name": "Errors",
                                "type": "counter",
                                "key": "proxy.process.http2.connection_errors",
                                "color": "#FF0000"
                            },
                        ]
                },
            ]
    },
    {
        "name": "Network & Errors",
        "panels":
            [
                {
                    "title": "Client Bytes/sec",
                    "metrics":
                        [
                            {
                                "name": "Read",
                                "type": "counter",
                                "key": "proxy.process.http.user_agent_total_request_bytes",
                                "color": "#00FFFF"
                            },
                            {
                                "name": "Write",
                                "type": "counter",
                                "key": "proxy.process.http.user_agent_total_response_bytes",
                                "color": "#FF00FF"
                            },
                        ]
                },
                {
                    "title": "Origin Bytes/sec",
                    "metrics":
                        [
                            {
                                "name": "Read",
                                "type": "counter",
                                "key": "proxy.process.http.origin_server_total_response_bytes",
                                "color": "#00FF00"
                            },
                            {
                                "name": "Write",
                                "type": "counter",
                                "key": "proxy.process.http.origin_server_total_request_bytes",
                                "color": "#FFA500"
                            },
                        ]
                },
                {
                    "title": "Connection Errors/sec",
                    "metrics":
                        [
                            {
                                "name": "Connect Fail",
                                "type": "counter",
                                "key": "proxy.process.http.err_connect_fail_count",
                                "color": "#FF0000"
                            },
                        ]
                },
                {
                    "title": "Transaction Errors/sec",
                    "metrics":
                        [
                            {
                                "name": "Aborts",
                                "type": "counter",
                                "key": "proxy.process.http.transaction_counts.errors.aborts",
                                "color": "#FFA500"
                            },
                        ]
                },
            ]
    },
]

# Command template for traffic_ctl
# Default path (adjust for your installation)
# Note: awk runs locally to avoid SSH quote escaping issues
METRIC_COMMAND_REMOTE = "/opt/edge/trafficserver/10.0/bin/traffic_ctl metric get {key}"
METRIC_COMMAND_LOCAL = "/opt/edge/trafficserver/10.0/bin/traffic_ctl metric get {key} | awk '{{print $2}}'"

# =============================================================================
# Terminal Utilities
# =============================================================================


def cursor_home():
    """Move cursor to top-left position."""
    sys.stdout.write('\033[H')
    sys.stdout.flush()


def clear_screen():
    """Clear the terminal screen."""
    sys.stdout.write('\033[2J\033[H')
    sys.stdout.flush()


def clear_scrollback():
    """Clear iTerm2 scrollback buffer to free memory from accumulated images."""
    # iTerm2-specific escape sequence to clear scrollback
    sys.stdout.write('\033]1337;ClearScrollback\007')
    # Also send standard escape sequence that works in some other terminals
    sys.stdout.write('\033[3J')
    sys.stdout.flush()


def hide_cursor():
    """Hide the terminal cursor."""
    sys.stdout.write('\033[?25l')
    sys.stdout.flush()


def show_cursor():
    """Show the terminal cursor."""
    sys.stdout.write('\033[?25h')
    sys.stdout.flush()


def imgcat_display(fig):
    """
    Display a matplotlib figure inline in iTerm2 using imgcat protocol.
    """
    buf = io.BytesIO()
    # Don't use bbox_inches='tight' - we want exact figure dimensions to fill terminal
    fig.savefig(buf, format='png', dpi=100, facecolor=fig.get_facecolor(), edgecolor='none')
    buf.seek(0)
    image_data = base64.b64encode(buf.read()).decode('ascii')
    buf.close()

    # iTerm2 inline image protocol
    sys.stdout.write(f'\033]1337;File=inline=1:{image_data}\a\n')
    sys.stdout.flush()


def format_value(value: float, is_percent: bool = False, is_latency: bool = False) -> str:
    """Format a value with K/M suffix for readability, or as percentage/latency."""
    if value is None:
        return "N/A"
    if is_percent:
        return f"{value:.0f}%"
    if is_latency:
        # Format latency in milliseconds
        if value >= 1000:
            return f"{value/1000:.1f}s"
        elif value >= 1:
            return f"{value:.0f}ms"
        else:
            return f"{value:.1f}ms"
    if abs(value) >= 1_000_000_000_000:
        return f"{value/1_000_000_000_000:.1f}T"
    if abs(value) >= 1_000_000_000:
        return f"{value/1_000_000_000:.1f}G"
    if abs(value) >= 1_000_000:
        return f"{value/1_000_000:.1f}M"
    elif abs(value) >= 1_000:
        return f"{value/1_000:.1f}K"
    elif abs(value) >= 1:
        return f"{value:.0f}"
    else:
        return f"{value:.2f}"


def format_ytick(value, pos):
    """Format Y-axis tick values with K/M/G/T suffixes, no decimals."""
    if value == 0:
        return '0'
    if abs(value) < 1:
        # Very small values - show with appropriate precision
        if abs(value) < 0.01:
            return '0'
        return f'{value:.1f}'
    if abs(value) >= 1e12:
        return f'{int(value/1e12)}T'
    if abs(value) >= 1e9:
        return f'{int(value/1e9)}G'
    if abs(value) >= 1e6:
        return f'{int(value/1e6)}M'
    if abs(value) >= 1e3:
        return f'{int(value/1e3)}K'
    return f'{int(value)}'


def get_terminal_pixel_size() -> Tuple[int, int]:
    """
    Get terminal size in pixels using TIOCGWINSZ ioctl.
    Returns (width, height) in pixels, or estimated size from rows/cols.
    """
    try:
        # Try to get pixel size from terminal
        result = fcntl.ioctl(sys.stdout.fileno(), termios.TIOCGWINSZ, b'\x00' * 8)
        rows, cols, xpixel, ypixel = struct.unpack('HHHH', result)

        if xpixel > 0 and ypixel > 0:
            return (xpixel, ypixel)

        # Estimate from rows/cols (typical cell: 9x18 pixels)
        return (cols * 9, rows * 18)
    except:
        # Fallback: assume reasonable terminal size
        size = shutil.get_terminal_size((80, 24))
        return (size.columns * 9, size.lines * 18)


def get_figure_size_for_terminal() -> Tuple[float, float]:
    """
    Calculate matplotlib figure size to fill terminal.
    Returns (width, height) in inches for matplotlib.
    """
    pixel_width, pixel_height = get_terminal_pixel_size()

    # imgcat displays at native resolution, so we want pixel dimensions
    # that match the terminal. Use 100 DPI as our reference.
    dpi = 100

    # Use nearly full terminal width/height - imgcat will display at native size
    # Leave minimal margin for terminal chrome
    width_inches = (pixel_width * 0.99) / dpi
    height_inches = (pixel_height * 0.92) / dpi  # Leave room for status bar

    # Minimum bounds for readability, no max - let it fill the terminal
    width_inches = max(12, width_inches)
    height_inches = max(8, height_inches)

    return (width_inches, height_inches)


class KeyboardHandler:
    """Non-blocking keyboard input handler."""

    def __init__(self):
        self.old_settings = None
        self.fd = None

    def __enter__(self):
        self.fd = sys.stdin.fileno()
        self.old_settings = termios.tcgetattr(self.fd)
        # Set raw mode for better key detection
        new_settings = termios.tcgetattr(self.fd)
        new_settings[3] = new_settings[3] & ~(termios.ICANON | termios.ECHO)
        new_settings[6][termios.VMIN] = 0
        new_settings[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSADRAIN, new_settings)
        return self

    def __exit__(self, *args):
        if self.old_settings and self.fd is not None:
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self.old_settings)

    def get_key(self) -> Optional[str]:
        """Get a keypress if available, non-blocking."""
        try:
            ch = os.read(self.fd, 1)
            if not ch:
                return None

            if ch == b'\x1b':  # Escape sequence
                # Read more bytes for arrow key sequences
                try:
                    ch2 = os.read(self.fd, 1)
                    if ch2 == b'[':
                        ch3 = os.read(self.fd, 1)
                        if ch3 == b'D':
                            return 'left'
                        elif ch3 == b'C':
                            return 'right'
                        elif ch3 == b'A':
                            return 'up'
                        elif ch3 == b'B':
                            return 'down'
                except:
                    pass
                return 'escape'
            elif ch in (b'q', b'Q'):
                return 'quit'
            elif ch in (b'h', b'H'):  # vim-style left
                return 'left'
            elif ch in (b'l', b'L'):  # vim-style right
                return 'right'
        except (OSError, IOError):
            pass
        return None


# =============================================================================
# Metric Collection
# =============================================================================

# JSONRPC script template - runs on remote host via SSH
JSONRPC_SCRIPT = '''
import socket
import json

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("{socket_path}")

request = {{
    "jsonrpc": "2.0",
    "method": "admin_lookup_records",
    "params": [{{"record_name_regex": "{pattern}"}}],
    "id": 1
}}
sock.sendall(json.dumps(request).encode() + b"\\n")
response = sock.recv(1048576).decode()
sock.close()

# Parse and output simple key=value format
data = json.loads(response)
for rec in data.get("result", {{}}).get("recordList", []):
    r = rec.get("record", {{}})
    name = r.get("record_name", "")
    value = r.get("current_value", "")
    dtype = r.get("data_type", "")
    print(f"{{name}}={{value}}={{dtype}}")
'''

# Default JSONRPC socket path (adjust for your installation)
JSONRPC_SOCKET_PATH = "/opt/edge/trafficserver/10.0/var/trafficserver/jsonrpc20.sock"


class JSONRPCBatchCollector:
    """
    Batch metric collector using JSONRPC Unix socket.
    Collects all metrics for a host in a single SSH call.
    """

    def __init__(self, hostname: str, metric_keys: list, socket_path: str = JSONRPC_SOCKET_PATH):
        self.hostname = hostname  # Bare hostname like "ats-server1.example.com"
        self.metric_keys = metric_keys
        self.socket_path = socket_path

        # Build regex pattern matching all metric keys
        # Escape dots and join with |
        escaped_keys = [k.replace('.', r'\.') for k in metric_keys]
        self.pattern = '|'.join(f"^{k}$" for k in escaped_keys)

        # Cached results from last collection
        self.last_values: dict = {}  # key -> (value, data_type)

    def collect(self) -> dict:
        """
        Collect all metrics in one SSH call.
        Returns dict of {metric_key: (value, data_type)}.
        """
        script = JSONRPC_SCRIPT.format(socket_path=self.socket_path, pattern=self.pattern)

        # Build SSH command - hostname is passed directly, we add "ssh" prefix
        # Encode script as base64 to avoid quoting issues
        script_b64 = base64.b64encode(script.encode()).decode()
        cmd = f"ssh {self.hostname} \"echo '{script_b64}' | base64 -d | python3\""

        try:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=10)

            if result.returncode != 0:
                return self.last_values

            # Parse output: key=value=dtype per line
            values = {}
            for line in result.stdout.strip().split('\n'):
                if '=' in line:
                    parts = line.split('=', 2)
                    if len(parts) >= 2:
                        key = parts[0]
                        try:
                            value = float(parts[1])
                        except:
                            continue
                        dtype = parts[2] if len(parts) > 2 else "INT"
                        values[key] = (value, dtype)

            self.last_values = values
            return values

        except (subprocess.TimeoutExpired, OSError):
            return self.last_values


class MetricCollector:
    """Collects metric values from a host via shell commands."""

    def __init__(self, name: str, key: str, metric_type: str, color: str, host_prefix: str = "", host_name: str = ""):
        self.name = name
        self.key = key
        self.metric_type = metric_type.lower()
        self.color = color
        self.host_prefix = host_prefix
        self.host_name = host_name

        # Build the full command
        if host_prefix:
            # For remote hosts: run traffic_ctl on remote, awk locally
            remote_cmd = METRIC_COMMAND_REMOTE.format(key=key)
            self.command = f"{host_prefix} '{remote_cmd}' | awk '{{print $2}}'"
        else:
            # Local: run everything locally
            self.command = METRIC_COMMAND_LOCAL.format(key=key)

        # For counter metrics, track previous value and time
        self._prev_value: Optional[float] = None
        self._prev_time: Optional[float] = None

        # Latest value for display
        self.latest_value: Optional[float] = None

    def _get_raw_value(self) -> Optional[float]:
        """Run the command and return the raw numeric value."""
        try:
            result = subprocess.run(self.command, shell=True, capture_output=True, text=True, timeout=5)
            if result.returncode != 0:
                return None

            output = result.stdout.strip()
            if not output:
                return None

            return float(output)
        except (subprocess.TimeoutExpired, ValueError, OSError):
            return None

    def collect(self) -> Optional[float]:
        """Collect the metric value."""
        raw_value = self._get_raw_value()
        current_time = time.time()

        if self.metric_type == 'gauge':
            self.latest_value = raw_value
            return raw_value

        # Counter: calculate rate of change
        if raw_value is None:
            return None

        if self._prev_value is None or self._prev_time is None:
            self._prev_value = raw_value
            self._prev_time = current_time
            return None

        time_delta = current_time - self._prev_time
        if time_delta <= 0:
            return None

        # Handle counter reset
        if raw_value < self._prev_value:
            self._prev_value = raw_value
            self._prev_time = current_time
            return None

        rate = (raw_value - self._prev_value) / time_delta
        self._prev_value = raw_value
        self._prev_time = current_time

        self.latest_value = rate
        return rate


# =============================================================================
# Main Grapher
# =============================================================================


class ATSGrapher:
    """Main grapher class with 2x2 grid layout and dark theme."""

    # Dark theme colors
    FIG_BG_COLOR = '#000000'  # Pure black for figure background (titles, labels area)
    PLOT_BG_COLOR = '#1a1a1a'  # Dark grey for inside the plots
    TEXT_COLOR = '#ffffff'
    GRID_COLOR = '#555555'  # Lighter grid for better visibility
    AXIS_COLOR = '#888888'  # Visible axis/spine color

    # Line styles for differentiating hosts (up to 4)
    LINE_STYLES = ['-', '--', '-.', ':']  # solid, dashed, dash-dot, dotted

    def __init__(
            self,
            hostnames: list,
            interval: float,
            history_seconds: int,
            pages: list,
            gui_mode: bool = False,
            save_png: Optional[str] = None,
            log_stats: Optional[str] = None,
            run_for: Optional[int] = None,
            no_keyboard: bool = False,
            tz_name: str = "UTC"):
        self.hostnames = hostnames  # Bare hostnames like ["ats-server1.example.com"]
        self.interval = interval
        self.history_seconds = history_seconds
        self.pages = pages
        self.gui_mode = gui_mode
        self.save_png = save_png
        self.log_stats = log_stats
        self.run_for = run_for
        self.no_keyboard = no_keyboard
        self.tz_name = tz_name
        self.num_hosts = len(hostnames)

        # Set up timezone
        if ZoneInfo and tz_name != "UTC":
            try:
                self.tz = ZoneInfo(tz_name)
            except:
                self.tz = timezone.utc
                self.tz_name = "UTC"
        else:
            self.tz = timezone.utc
            if tz_name != "UTC":
                self.tz_name = "UTC"  # Fallback if zoneinfo not available

        self.current_page = 0
        self.running = True
        self.start_time = time.time()
        self.iteration = 0

        # Extract short host names for display (e.g., "e1" from "ats-server1.example.com")
        self.host_names = [self._extract_short_name(h) for h in hostnames]

        # Initialize log file if specified
        if self.log_stats:
            with open(self.log_stats, 'w') as f:
                f.write(f"# Traffic Grapher Stats Log - {datetime.now()}\n")
                f.write(f"# Hosts: {', '.join(hostnames)}\n")
                f.write("#\n")

        # Collect ALL unique metric keys across ALL pages for combined batch collection
        all_metric_keys = []
        for page in pages:
            for panel in page["panels"]:
                for metric in panel["metrics"]:
                    if metric["key"] not in all_metric_keys:
                        all_metric_keys.append(metric["key"])
                    # Also collect key2 for hit_rate metrics
                    if "key2" in metric and metric["key2"] not in all_metric_keys:
                        all_metric_keys.append(metric["key2"])

        # Create ONE combined batch collector per host (collects all metrics at once)
        self.combined_collectors = [JSONRPCBatchCollector(hostname, all_metric_keys) for hostname in hostnames]

        # Initialize metric info and data for all pages
        # metric_info[page][panel][metric] = {name, key, type, color}
        # data[page][panel][metric][host] = deque of (elapsed_time, value)
        self.metric_info: list = []
        self.data: list = []

        # Track previous values for counter rate calculation
        # prev_values[page][panel][metric][host] = (prev_raw, prev_time)
        self.prev_values: list = []

        # Track latest values for display
        # latest_values[page][panel][metric][host] = value
        self.latest_values: list = []

        for page in pages:
            page_info = []
            page_data = []
            page_prev = []
            page_latest = []
            for panel in page["panels"]:
                panel_info = []
                panel_data = []
                panel_prev = []
                panel_latest = []
                for metric in panel["metrics"]:
                    info = {"name": metric["name"], "key": metric["key"], "type": metric["type"].lower(), "color": metric["color"]}
                    # Store key2 for hit_rate metrics
                    if "key2" in metric:
                        info["key2"] = metric["key2"]
                    panel_info.append(info)

                    # Initialize data structures for each host
                    metric_data = [deque() for _ in range(self.num_hosts)]
                    metric_prev = [None for _ in range(self.num_hosts)]
                    metric_latest = [None for _ in range(self.num_hosts)]
                    panel_data.append(metric_data)
                    panel_prev.append(metric_prev)
                    panel_latest.append(metric_latest)
                page_info.append(panel_info)
                page_data.append(panel_data)
                page_prev.append(panel_prev)
                page_latest.append(panel_latest)
            self.metric_info.append(page_info)
            self.data.append(page_data)
            self.prev_values.append(page_prev)
            self.latest_values.append(page_latest)

        # Terminal size tracking
        self.last_terminal_size = shutil.get_terminal_size()

    def _extract_short_name(self, hostname: str) -> str:
        """Extract a short display name from a full hostname."""
        if not hostname:
            return "local"
        # Take first part before dot (e.g., "e1" from "ats-server1.example.com")
        if '.' in hostname:
            return hostname.split('.')[0]
        return hostname[:15]

    def _check_resize(self) -> bool:
        """Check if terminal was resized."""
        current_size = shutil.get_terminal_size()
        if current_size != self.last_terminal_size:
            self.last_terminal_size = current_size
            return True
        return False

    def _trim_history(self):
        """Remove old data points outside the history window."""
        cutoff = time.time() - self.start_time - self.history_seconds
        for page_data in self.data:
            for panel_data in page_data:
                for metric_data in panel_data:
                    for host_data in metric_data:
                        while host_data and host_data[0][0] < cutoff:
                            host_data.popleft()

    def collect_all_pages(self):
        """Collect data for ALL pages in a single batch per host."""
        self.iteration += 1

        log_lines = []
        if self.log_stats:
            elapsed = time.time() - self.start_time
            log_lines.append(f"\n=== Iteration {self.iteration} at {elapsed:.1f}s ===\n")

        # Batch collect from each host - capture time PER HOST
        batch_results = []
        host_times = []

        for host_idx, batch_collector in enumerate(self.combined_collectors):
            host_label = self.host_names[host_idx]

            if self.log_stats:
                log_lines.append(f"  Collecting from {host_label}...\n")

            collect_time = time.time()
            results = batch_collector.collect()
            batch_results.append(results)
            host_times.append(collect_time)

            if self.log_stats:
                log_lines.append(f"    Got {len(results)} metrics\n")

        # Distribute values to ALL pages
        for page_idx in range(len(self.pages)):
            for panel_idx, panel_info in enumerate(self.metric_info[page_idx]):
                panel = self.pages[page_idx]["panels"][panel_idx]

                for metric_idx, metric in enumerate(panel_info):
                    key = metric["key"]
                    metric_type = metric["type"]

                    for host_idx in range(self.num_hosts):
                        host_label = self.host_names[host_idx]
                        current_time = host_times[host_idx]

                        if key in batch_results[host_idx]:
                            raw_value, dtype = batch_results[host_idx][key]

                            # Calculate value based on metric type
                            if metric_type == 'gauge':
                                value = raw_value
                                self.latest_values[page_idx][panel_idx][metric_idx][host_idx] = value
                            elif metric_type == 'hit_rate':
                                # Calculate hit rate percentage: hits / (hits + misses) * 100
                                key2 = metric.get("key2")
                                if key2 and key2 in batch_results[host_idx]:
                                    hits_raw = raw_value
                                    misses_raw, _ = batch_results[host_idx][key2]

                                    prev = self.prev_values[page_idx][panel_idx][metric_idx][host_idx]
                                    if prev is None:
                                        # Store both hits and misses as prev value
                                        self.prev_values[page_idx][panel_idx][metric_idx][host_idx] = (
                                            (hits_raw, misses_raw), current_time)
                                        value = None
                                    else:
                                        (prev_hits, prev_misses), prev_time = prev
                                        time_delta = current_time - prev_time
                                        if time_delta > 0:
                                            delta_hits = hits_raw - prev_hits
                                            delta_misses = misses_raw - prev_misses
                                            total = delta_hits + delta_misses
                                            if total > 0:
                                                value = (delta_hits / total) * 100.0
                                                self.latest_values[page_idx][panel_idx][metric_idx][host_idx] = value
                                            else:
                                                value = None
                                        else:
                                            value = None
                                        self.prev_values[page_idx][panel_idx][metric_idx][host_idx] = (
                                            (hits_raw, misses_raw), current_time)
                                else:
                                    value = None
                            elif metric_type == 'latency':
                                # Calculate average latency: delta_time / delta_count
                                # key = total time counter (milliseconds), key2 = count counter
                                key2 = metric.get("key2")
                                if key2 and key2 in batch_results[host_idx]:
                                    time_raw = raw_value  # Total time in milliseconds
                                    count_raw, _ = batch_results[host_idx][key2]

                                    prev = self.prev_values[page_idx][panel_idx][metric_idx][host_idx]
                                    if prev is None:
                                        self.prev_values[page_idx][panel_idx][metric_idx][host_idx] = (
                                            (time_raw, count_raw), current_time)
                                        value = None
                                    else:
                                        (prev_time_raw, prev_count), prev_time = prev
                                        delta_time_ms = time_raw - prev_time_raw
                                        delta_count = count_raw - prev_count
                                        if delta_count > 0 and delta_time_ms >= 0:
                                            # Average latency already in milliseconds
                                            value = delta_time_ms / delta_count
                                            self.latest_values[page_idx][panel_idx][metric_idx][host_idx] = value
                                        else:
                                            value = None
                                        self.prev_values[page_idx][panel_idx][metric_idx][host_idx] = (
                                            (time_raw, count_raw), current_time)
                                else:
                                    value = None
                            else:
                                # Counter: calculate rate
                                prev = self.prev_values[page_idx][panel_idx][metric_idx][host_idx]
                                if prev is None:
                                    self.prev_values[page_idx][panel_idx][metric_idx][host_idx] = (raw_value, current_time)
                                    value = None
                                else:
                                    prev_raw, prev_time = prev
                                    time_delta = current_time - prev_time
                                    if time_delta > 0 and raw_value >= prev_raw:
                                        value = (raw_value - prev_raw) / time_delta
                                        self.latest_values[page_idx][panel_idx][metric_idx][host_idx] = value
                                    else:
                                        value = None
                                    self.prev_values[page_idx][panel_idx][metric_idx][host_idx] = (raw_value, current_time)

                            if value is not None:
                                elapsed = current_time - self.start_time
                                self.data[page_idx][panel_idx][metric_idx][host_idx].append((elapsed, value))

        if self.log_stats:
            # Log current page data point counts only
            page_idx = self.current_page
            log_lines.append(f"  Data points (page {page_idx + 1}):\n")
            for panel_idx, panel_data in enumerate(self.data[page_idx]):
                panel = self.pages[page_idx]["panels"][panel_idx]
                for metric_idx, metric_data in enumerate(panel_data):
                    metric = self.metric_info[page_idx][panel_idx][metric_idx]
                    for host_idx, host_data in enumerate(metric_data):
                        host_label = self.host_names[host_idx]
                        log_lines.append(f"    [{panel['title']}] {metric['name']} ({host_label}): {len(host_data)} points\n")

            with open(self.log_stats, 'a') as f:
                f.writelines(log_lines)

        self._trim_history()

    def collect_data(self):
        """Collect data - wrapper that calls collect_all_pages for backwards compatibility."""
        self.collect_all_pages()

    def render_page(self, fig: plt.Figure = None) -> plt.Figure:
        """Render the current page as a matplotlib figure with 2x2 grid."""
        page = self.pages[self.current_page]
        current_elapsed = time.time() - self.start_time

        # Set up dark theme - dynamic figure size to fill terminal
        plt.style.use('dark_background')
        fig_width, fig_height = get_figure_size_for_terminal()

        if fig is None:
            # Create new figure
            fig, axes = plt.subplots(2, 2, figsize=(fig_width, fig_height))
        else:
            # Reuse existing figure, create new axes
            fig.set_size_inches(fig_width, fig_height)
            axes = fig.subplots(2, 2)

        fig.patch.set_facecolor(self.FIG_BG_COLOR)  # Pure black outside graphs

        # Flatten axes for easier iteration
        axes_flat = axes.flatten()

        # Y-axis formatter for K/M/G/T
        y_formatter = FuncFormatter(format_ytick)

        num_hosts = self.num_hosts

        for panel_idx, panel in enumerate(page["panels"]):
            if panel_idx >= 4:
                break
            ax = axes_flat[panel_idx]
            ax.set_facecolor(self.PLOT_BG_COLOR)  # Dark grey inside plots

            panel_data = self.data[self.current_page][panel_idx]
            panel_info = self.metric_info[self.current_page][panel_idx]
            panel_latest = self.latest_values[self.current_page][panel_idx]

            # Build title with current values
            value_parts = []
            metric_values = {}  # Group values by metric for compact display
            has_data = False

            for metric_idx, metric in enumerate(panel_info):
                for host_idx in range(num_hosts):
                    host_data = panel_data[metric_idx][host_idx]
                    host_name = self.host_names[host_idx]

                    # Plot the data - transform X to "seconds ago"
                    if host_data:
                        has_data = True
                        # Transform: x = current_elapsed - data_elapsed = age of data point
                        # Newest data (age ~0) on RIGHT, oldest data on LEFT
                        times = [current_elapsed - t for t, _ in host_data]
                        values = [v for _, v in host_data]

                        color = metric["color"]
                        linestyle = self.LINE_STYLES[host_idx % len(self.LINE_STYLES)]
                        linewidth = 2.5 - (host_idx * 0.2)  # Slightly thinner for each host

                        label = metric["name"]
                        if self.num_hosts > 1:
                            label = f"{metric['name']} ({host_name})"

                        ax.plot(times, values, color=color, linewidth=linewidth, linestyle=linestyle, label=label)

                    # Collect values per metric for compact display
                    latest = panel_latest[metric_idx][host_idx]
                    if latest is not None:
                        if metric_idx not in metric_values:
                            metric_values[metric_idx] = {'name': metric['name'], 'type': metric['type'], 'values': []}
                        is_percent = metric['type'] == 'hit_rate'
                        is_latency = metric['type'] == 'latency'
                        metric_values[metric_idx]['values'].append(format_value(latest, is_percent, is_latency))

            # Build compact title: "Requests/sec - Client: 3.6K/4.0K | Origin: 2.5K/2.7K"
            for m_idx, m_data in metric_values.items():
                if m_data['values']:
                    if self.num_hosts > 1:
                        value_parts.append(f"{m_data['name']}: {'/'.join(m_data['values'])}")
                    else:
                        value_parts.append(f"{m_data['name']}: {m_data['values'][0]}")

            # Set title with values
            if value_parts:
                title = f"{panel['title']} - {' | '.join(value_parts)}"
            else:
                title = panel["title"]
            ax.set_title(title, color=self.TEXT_COLOR, fontsize=12, fontweight='bold')

            # Configure axes - more visible grid and borders
            ax.grid(True, alpha=0.6, color=self.GRID_COLOR, linewidth=0.8)
            ax.tick_params(colors=self.TEXT_COLOR, labelsize=12)
            for spine in ax.spines.values():
                spine.set_color(self.AXIS_COLOR)
                spine.set_linewidth(1.5)

            # X-axis: oldest on left, 0 (now) on right - data flows right to left
            ax.set_xlim(self.history_seconds, 0)  # Reversed: oldest on left, 0=now on right
            ax.set_xlabel('')  # No label - self explanatory
            ax.tick_params(axis='x', labelsize=14)

            # Check if this is a percentage or latency panel
            is_percent_panel = any(m['type'] == 'hit_rate' for m in panel_info)
            is_latency_panel = any(m['type'] == 'latency' for m in panel_info)

            if is_percent_panel:
                # Percentage panel: fixed 0-100% range, no K/M/G formatter
                ax.set_ylim(0, 100)
                ax.set_ylabel("Hit Rate %", color=self.TEXT_COLOR, fontsize=14, fontweight='bold')
                ax.tick_params(axis='y', labelsize=14)
            elif is_latency_panel:
                # Latency panel: auto-scale, Y-axis label in ms
                ax.set_ylabel("Latency (ms)", color=self.TEXT_COLOR, fontsize=14, fontweight='bold')
                ax.tick_params(axis='y', labelsize=14)
            else:
                # Y-axis: use K/M/G/T formatter, no decimals, add label
                ax.yaxis.set_major_formatter(y_formatter)
                ax.tick_params(axis='y', labelsize=14)

                # Y-axis label based on panel title
                ylabel = panel["title"].split('/')[0].strip() if '/' in panel["title"] else ""
                if ylabel:
                    ax.set_ylabel(ylabel, color=self.TEXT_COLOR, fontsize=14, fontweight='bold')

            # Legend if multiple metrics or hosts (only if we have data)
            if has_data and (len(panel["metrics"]) > 1 or self.num_hosts > 1):
                ax.legend(
                    loc='upper left',
                    fontsize=13,
                    facecolor=self.PLOT_BG_COLOR,
                    edgecolor=self.AXIS_COLOR,
                    labelcolor=self.TEXT_COLOR)

        # Overall title with date, time, and timezone
        now = datetime.now(self.tz)
        timestamp = now.strftime('%Y-%m-%d %H:%M:%S')
        title = f"ATS Dashboard - {timestamp} {self.tz_name} - Page {self.current_page + 1}/{len(self.pages)}: {page['name']}"
        if self.num_hosts > 1:
            title += f"\n{' vs '.join(self.host_names)}"
        else:
            title += f" ({self.host_names[0]})"
        fig.suptitle(title, color=self.TEXT_COLOR, fontsize=18, fontweight='bold')

        # Status bar
        status = f"[←/→ or h/l pages, q quit] | {self.interval}s refresh | {self.history_seconds}s history"
        fig.text(0.5, 0.01, status, ha='center', fontsize=13, color='#808080')

        plt.tight_layout()
        plt.subplots_adjust(top=0.92, bottom=0.06, left=0.06, right=0.98, hspace=0.35, wspace=0.22)

        # Save PNG if requested
        if self.save_png:
            png_path = self.save_png
            if '{iter}' in png_path:
                png_path = png_path.replace('{iter}', str(self.iteration))
            fig.savefig(png_path, facecolor=fig.get_facecolor(), edgecolor='none', dpi=100)

        return fig

    def run_imgcat(self):
        """Run the grapher in imgcat mode."""
        run_until = None
        if self.run_for:
            run_until = time.time() + self.run_for

        # Non-keyboard mode (for non-TTY environments)
        if self.no_keyboard:
            try:
                while self.running:
                    if run_until and time.time() >= run_until:
                        print(f"\n\nRun time of {self.run_for}s reached. Exiting.")
                        break

                    self.collect_data()
                    fig = self.render_page()

                    # In no-keyboard mode, just save the PNG, don't try imgcat
                    if self.save_png:
                        print(f"Iteration {self.iteration}: saved to {self.save_png}")
                    plt.close(fig)
                    plt.close('all')  # Close any remaining figures
                    gc.collect()  # Force garbage collection to reduce memory

                    # Clear iTerm scrollback every 60 iterations to prevent memory buildup
                    if self.iteration % 60 == 0:
                        clear_scrollback()

                    time.sleep(self.interval)
            except KeyboardInterrupt:
                print("\nStopped by user")
            return

        # Normal interactive mode
        hide_cursor()
        clear_screen()

        try:
            with KeyboardHandler() as kbd:
                while self.running:
                    # Check run_for timeout
                    if run_until and time.time() >= run_until:
                        print(f"\n\nRun time of {self.run_for}s reached. Exiting.")
                        break

                    # Check for resize
                    if self._check_resize():
                        clear_screen()
                    else:
                        cursor_home()

                    # Collect data and render
                    self.collect_data()
                    fig = self.render_page()
                    imgcat_display(fig)
                    plt.close(fig)
                    plt.close('all')
                    gc.collect()

                    # Clear iTerm scrollback every 60 iterations to prevent memory buildup
                    if self.iteration % 60 == 0:
                        clear_scrollback()

                    # Handle keyboard input during sleep
                    sleep_until = time.time() + self.interval
                    while time.time() < sleep_until and self.running:
                        # Check run_for timeout during sleep too
                        if run_until and time.time() >= run_until:
                            break
                        key = kbd.get_key()
                        if key == 'quit':
                            self.running = False
                            break
                        elif key == 'left':
                            self.current_page = (self.current_page - 1) % len(self.pages)
                            break
                        elif key == 'right':
                            self.current_page = (self.current_page + 1) % len(self.pages)
                            break
                        time.sleep(0.05)
        finally:
            show_cursor()
            clear_screen()

    def run_gui(self):
        """Run the grapher in GUI mode with matplotlib window."""
        from matplotlib.animation import FuncAnimation

        # Collect initial data for all pages
        self.collect_all_pages()

        # Create initial figure
        fig = self.render_page()

        # Calculate number of frames if run_for specified
        if self.run_for:
            num_frames = int(self.run_for / self.interval)
            repeat = False
        else:
            num_frames = None  # Infinite
            repeat = True

        def update(frame):
            self.collect_all_pages()
            fig.clf()  # Clear the existing figure, don't create new one
            # Re-render onto the same figure
            self.render_page(fig=fig)
            fig.canvas.draw_idle()
            return []

        anim = FuncAnimation(
            fig, update, interval=int(self.interval * 1000), frames=num_frames, repeat=repeat, blit=False, cache_frame_data=False)
        plt.show()

    def run_once(self):
        """Run a single snapshot and exit."""
        self.collect_data()
        time.sleep(self.interval)
        self.collect_data()

        fig = self.render_page()
        imgcat_display(fig)
        plt.close(fig)

    def run(self, once: bool = False):
        """Run the grapher."""
        if once:
            self.run_once()
        elif self.gui_mode:
            self.run_gui()
        else:
            self.run_imgcat()


# =============================================================================
# Configuration Loading
# =============================================================================


def load_config(config_path: str) -> dict:
    """Load optional config file for layout customization."""
    if not os.path.exists(config_path):
        return {}

    with open(config_path, 'r') as f:
        return yaml.safe_load(f) or {}


# =============================================================================
# Main Entry Point
# =============================================================================


def main():
    parser = argparse.ArgumentParser(
        description='Traffic Grapher - Real-time ATS metrics visualization',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s ats-server1.example.com
  %(prog)s ats-server{1..3}.example.com                    # bash expansion
  %(prog)s --interval 2 --history 120 ats-server{1..4}.example.com
  %(prog)s ats1.dc1.example.com ats1.dc2.example.com   # compare POPs
""")

    parser.add_argument(
        'hosts', nargs='+', metavar='HOSTNAME', help='Hostnames to monitor (1-4 hosts, e.g., ats-server1.example.com)')
    parser.add_argument('--interval', type=float, default=1.0, help='Refresh interval in seconds (default: 1.0)')
    parser.add_argument('--history', type=int, default=60, help='History window in seconds (default: 60)')
    parser.add_argument('--gui', action='store_true', help='Use matplotlib GUI window instead of imgcat')
    parser.add_argument('--once', action='store_true', help='Single snapshot, then exit')
    parser.add_argument('-c', '--config', default=None, help='Optional config file for layout customization')

    # Display options
    parser.add_argument('--timezone', '-tz', default='UTC', help='Timezone for display (default: UTC, e.g., America/Los_Angeles)')

    # Debug options
    parser.add_argument(
        '--save-png', default=None, metavar='FILE', help='Save PNG to file after each render (use {iter} for iteration number)')
    parser.add_argument('--log-stats', default=None, metavar='FILE', help='Log raw stats to file for debugging')
    parser.add_argument('--run-for', type=int, default=None, metavar='SECONDS', help='Run for N seconds then exit (for debugging)')
    parser.add_argument('--no-keyboard', action='store_true', help='Disable keyboard handling (for non-TTY environments)')

    args = parser.parse_args()

    # Load optional config
    config = {}
    if args.config:
        config = load_config(args.config)

    # Use custom pages from config, or defaults
    pages = config.get('pages', DEFAULT_PAGES)

    # Override history from config if specified
    history = args.history
    if 'history' in config:
        history = config['history'].get('seconds', args.history)

    # Get timezone from config or CLI
    tz_name = args.timezone
    if 'timezone' in config:
        tz_name = config['timezone']

    # Validate host count
    if len(args.hosts) > 4:
        parser.error("Maximum 4 hosts supported (limited by line styles)")

    # Create and run grapher
    grapher = ATSGrapher(
        hostnames=args.hosts,
        interval=args.interval,
        history_seconds=history,
        pages=pages,
        gui_mode=args.gui,
        save_png=args.save_png,
        log_stats=args.log_stats,
        run_for=args.run_for,
        no_keyboard=args.no_keyboard,
        tz_name=tz_name)

    print(f"Traffic Grapher - {len(pages)} pages, {args.interval}s refresh, {history}s history")
    if len(args.hosts) > 1:
        print(f"Comparing: {' vs '.join(grapher.host_names)}")
    else:
        print(f"Monitoring: {grapher.host_names[0]}")
    print("Starting in 2 seconds... (press Ctrl+C to cancel)")
    time.sleep(2)

    # Clear scrollback buffer at startup to free any previous accumulated images
    clear_scrollback()

    try:
        grapher.run(once=args.once)
    except KeyboardInterrupt:
        print("\nStopped by user")
        return 0

    return 0


if __name__ == '__main__':
    sys.exit(main())
