#!/usr/bin/env python3
"""
Generate graph layouts for traffic_top using Unicode block characters.

Uses vertical bar graphs with block characters:
▁ ▂ ▃ ▄ ▅ ▆ ▇ █ (heights 1-8)

Color gradient (ANSI escape codes):
- Low values: Blue/Cyan
- Medium values: Green/Yellow
- High values: Orange/Red
"""

# Unicode block characters for vertical bars (index 0 = empty, 1-8 = heights)
BLOCKS = [' ', '▁', '▂', '▃', '▄', '▅', '▆', '▇', '█']

# ANSI color codes for gradient (blue -> cyan -> green -> yellow -> red)
COLORS = {
    'reset': '\033[0m',
    'blue': '\033[34m',
    'cyan': '\033[36m',
    'green': '\033[32m',
    'yellow': '\033[33m',
    'red': '\033[31m',
    'magenta': '\033[35m',
    'white': '\033[37m',
    'bold': '\033[1m',
    'dim': '\033[2m',
}


def value_to_block(value: float, max_val: float = 100.0) -> str:
    """Convert a value (0-max_val) to a block character."""
    if value <= 0:
        return BLOCKS[0]
    normalized = min(value / max_val, 1.0)
    index = int(normalized * 8)
    return BLOCKS[min(index + 1, 8)] if normalized > 0 else BLOCKS[0]


def value_to_color(value: float, max_val: float = 100.0) -> str:
    """Get color code based on value (gradient from blue to red)."""
    normalized = min(value / max_val, 1.0)
    if normalized < 0.2:
        return COLORS['blue']
    elif normalized < 0.4:
        return COLORS['cyan']
    elif normalized < 0.6:
        return COLORS['green']
    elif normalized < 0.8:
        return COLORS['yellow']
    else:
        return COLORS['red']


def generate_graph_data(length: int, pattern: str = 'wave') -> list:
    """Generate sample data for graph demonstration."""
    import math

    if pattern == 'wave':
        # Sine wave pattern
        return [50 + 40 * math.sin(i * 0.3) for i in range(length)]
    elif pattern == 'ramp':
        # Rising pattern
        return [min(100, i * 100 / length) for i in range(length)]
    elif pattern == 'spike':
        # Random spikes
        import random
        random.seed(42)
        return [random.randint(10, 90) for _ in range(length)]
    elif pattern == 'load':
        # Realistic CPU/network load pattern
        import random
        random.seed(123)
        base = 30
        data = []
        for i in range(length):
            base = max(5, min(95, base + random.randint(-15, 15)))
            data.append(base)
        return data
    else:
        return [50] * length


def format_graph_line(data: list, width: int, colored: bool = False) -> str:
    """Format a single line of graph from data points."""
    # Take last 'width' data points, or pad with zeros
    if len(data) > width:
        data = data[-width:]
    elif len(data) < width:
        data = [0] * (width - len(data)) + data

    result = ""
    for val in data:
        block = value_to_block(val)
        if colored:
            color = value_to_color(val)
            result += f"{color}{block}{COLORS['reset']}"
        else:
            result += block
    return result


def format_header(title: str, box_width: int) -> str:
    """Format a box header."""
    content_width = box_width - 2
    title_with_spaces = f" {title} "
    dashes_needed = content_width - len(title_with_spaces)
    left_dashes = dashes_needed // 2
    right_dashes = dashes_needed - left_dashes
    return f"+{'-' * left_dashes}{title_with_spaces}{'-' * right_dashes}+"


def format_separator(box_width: int) -> str:
    """Format a box separator."""
    return f"+{'-' * (box_width - 2)}+"


def format_graph_box_40(title: str, data: list, current_val: str, max_val: str, show_color: bool = False) -> list:
    """
    Generate a 40-character wide box with graph (title in header).

    Layout:
    +---- TITLE (current: XX%) ----+
    | ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▄▅▆ |
    | Min: 0%              Max: 100% |
    +-------------------------------+
    """
    lines = []
    graph_width = 36  # 40 - 2 borders - 2 padding

    # Header with current value
    header_title = f"{title} ({current_val})"
    lines.append(format_header(header_title, 40))

    # Graph line
    graph = format_graph_line(data, graph_width, colored=show_color)
    lines.append(f"| {graph} |")

    # Min/Max labels line
    min_label = "Min: 0%"
    max_label = f"Max: {max_val}"
    space_between = graph_width - len(min_label) - len(max_label)
    lines.append(f"| {min_label}{' ' * space_between}{max_label} |")

    lines.append(format_separator(40))
    return lines


def format_graph_row(label: str, data: list, value: str, width: int, show_color: bool = False) -> str:
    """
    Format a single graph row with title inside: | LABEL  ▁▂▃▄▅  VALUE |

    Used for multi-graph boxes where each row is a separate metric.
    """
    content_width = width - 4  # subtract "| " and " |"

    # Allocate space: label (fixed), graph (flexible), value (fixed)
    value_width = len(value) + 1  # value + leading space
    label_width = min(len(label), 12)  # max 12 chars for label
    graph_width = content_width - label_width - value_width - 1  # -1 for space after label

    # Build the line
    label_part = label[:label_width].ljust(label_width)
    graph_part = format_graph_line(data, graph_width, colored=show_color)
    value_part = value.rjust(value_width)

    return f"| {label_part} {graph_part}{value_part} |"


def format_multi_graph_box(graphs: list, width: int = 40, title: str = None, show_color: bool = False) -> list:
    """
    Generate a box with multiple graphs inside (titles inside box).

    Each graph entry: (label, data, value)

    Layout (40-char):
    +--------------------------------------+
    | Bandwidth  ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂    850M |
    | Hit Rate   ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂     85% |
    | Requests   ▂▂▄▄▄▃▂▇▂▇▆▂▂▂▃▄▆▇      15K |
    +--------------------------------------+
    """
    lines = []

    # Header (plain separator or with title)
    if title:
        lines.append(format_header(title, width))
    else:
        lines.append(format_separator(width))

    # Graph rows
    for label, data, value in graphs:
        lines.append(format_graph_row(label, data, value, width, show_color))

    # Footer
    lines.append(format_separator(width))

    return lines


def format_graph_box_80(title: str, data: list, current_val: str, avg_val: str, max_val: str, show_color: bool = False) -> list:
    """
    Generate an 80-character wide box with graph and stats.

    Layout:
    +----------------------- NETWORK BANDWIDTH (850 Mb/s) ------------------------+
    | ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▄▅ |
    | Min: 0 Mb/s          Avg: 620 Mb/s          Max: 1000 Mb/s          60s ago |
    +-----------------------------------------------------------------------------+
    """
    lines = []
    graph_width = 76  # 80 - 2 borders - 2 padding

    # Header with current value
    header_title = f"{title} ({current_val})"
    lines.append(format_header(header_title, 80))

    # Graph line
    graph = format_graph_line(data, graph_width, colored=show_color)
    lines.append(f"| {graph} |")

    # Stats line: Min, Avg, Max, Time
    min_label = "Min: 0"
    avg_label = f"Avg: {avg_val}"
    max_label = f"Max: {max_val}"
    time_label = "60s ago"

    # Distribute labels across the width
    total_label_len = len(min_label) + len(avg_label) + len(max_label) + len(time_label)
    remaining = graph_width - total_label_len
    gap = remaining // 3

    stats_line = f"{min_label}{' ' * gap}{avg_label}{' ' * gap}{max_label}{' ' * gap}{time_label}"
    # Pad to exact width
    stats_line = stats_line.ljust(graph_width)
    lines.append(f"| {stats_line} |")

    lines.append(format_separator(80))
    return lines


def format_multi_graph_box_80(graphs: list, show_color: bool = False) -> list:
    """
    Generate an 80-character wide box with multiple stacked graphs.

    Each graph entry: (title, data, current_val)
    """
    lines = []
    content_width = 76  # 80 - 2 borders - 2 padding

    # Combined header
    titles = " / ".join(g[0] for g in graphs)
    lines.append(format_header(titles, 80))

    for title, data, current_val in graphs:
        # Label and graph on same line
        label = f"{title}: {current_val}"
        label_width = 18  # Fixed label width
        graph_width = content_width - label_width  # Remaining for graph

        graph = format_graph_line(data, graph_width, colored=show_color)
        line_content = f"{label:<{label_width}}{graph}"
        lines.append(f"| {line_content} |")

    lines.append(format_separator(80))
    return lines


def print_ascii_layout():
    """Print ASCII-only version (for documentation)."""
    print("## Graph Layouts (ASCII for documentation)")
    print()
    print("### 40-Character Box with Graph")
    print()
    print("```")

    # Generate sample data
    data = generate_graph_data(36, 'load')

    for line in format_graph_box_40("CPU", data, "45%", "100%", show_color=False):
        print(line)

    print()

    data = generate_graph_data(36, 'wave')
    for line in format_graph_box_40("HIT RATE", data, "85%", "100%", show_color=False):
        print(line)

    print("```")
    print()
    print("### 80-Character Box with Graph")
    print()
    print("```")

    data = generate_graph_data(76, 'load')
    for line in format_graph_box_80("NETWORK BANDWIDTH", data, "850 Mb/s", "620 Mb/s", "1000 Mb/s", show_color=False):
        print(line)

    print()

    data = generate_graph_data(76, 'wave')
    for line in format_graph_box_80("CACHE HIT RATE", data, "85%", "78%", "100%", show_color=False):
        print(line)

    print("```")
    print()
    print("### 80-Character Box with Multiple Graphs")
    print()
    print("```")

    # graph_width = 76 - 18 (label) = 58
    graphs = [
        ("Net In", generate_graph_data(58, 'load'), "850 Mb/s"),
        ("Net Out", generate_graph_data(58, 'wave'), "620 Mb/s"),
        ("Req/sec", generate_graph_data(58, 'spike'), "15K"),
    ]
    for line in format_multi_graph_box_80(graphs, show_color=False):
        print(line)

    print("```")


def print_colored_demo():
    """Print colored version to terminal."""
    print()
    print(f"{COLORS['bold']}## Graph Demo with Colors{COLORS['reset']}")
    print()

    print(f"{COLORS['cyan']}### 40-Character Box{COLORS['reset']}")
    print()

    data = generate_graph_data(36, 'load')
    for line in format_graph_box_40("CPU USAGE", data, "45%", "100%", show_color=True):
        print(line)

    print()

    print(f"{COLORS['cyan']}### 80-Character Box{COLORS['reset']}")
    print()

    data = generate_graph_data(76, 'load')
    for line in format_graph_box_80("NETWORK BANDWIDTH", data, "850 Mb/s", "620 Mb/s", "1000 Mb/s", show_color=True):
        print(line)

    print()

    print(f"{COLORS['cyan']}### Multi-Graph Box{COLORS['reset']}")
    print()

    graphs = [
        ("Net In", generate_graph_data(58, 'load'), "850 Mb/s"),
        ("Net Out", generate_graph_data(58, 'wave'), "620 Mb/s"),
        ("Req/sec", generate_graph_data(58, 'spike'), "15K"),
        ("Hit Rate", generate_graph_data(58, 'ramp'), "85%"),
    ]
    for line in format_multi_graph_box_80(graphs, show_color=True):
        print(line)

    print()
    print(f"{COLORS['dim']}Color gradient: ", end="")
    for i in range(0, 101, 10):
        color = value_to_color(i)
        block = value_to_block(i)
        print(f"{color}{block}{COLORS['reset']}", end="")
    print(f" (0% to 100%){COLORS['reset']}")
    print()


def print_block_reference():
    """Print reference of available block characters."""
    print()
    print("## Block Character Reference")
    print()
    print("Unicode block characters used for graphs:")
    print()
    print("| Height | Char | Unicode | Description |")
    print("|--------|------|---------|-------------|")
    print("|   0    |  ' ' | U+0020  | Empty/space |")
    print("|   1    |  ▁   | U+2581  | Lower 1/8   |")
    print("|   2    |  ▂   | U+2582  | Lower 2/8   |")
    print("|   3    |  ▃   | U+2583  | Lower 3/8   |")
    print("|   4    |  ▄   | U+2584  | Lower 4/8   |")
    print("|   5    |  ▅   | U+2585  | Lower 5/8   |")
    print("|   6    |  ▆   | U+2586  | Lower 6/8   |")
    print("|   7    |  ▇   | U+2587  | Lower 7/8   |")
    print("|   8    |  █   | U+2588  | Full block  |")
    print()
    print("Visual scale: ▁▂▃▄▅▆▇█")
    print()


if __name__ == "__main__":
    import sys

    if "--color" in sys.argv or "-c" in sys.argv:
        print_colored_demo()
    elif "--blocks" in sys.argv or "-b" in sys.argv:
        print_block_reference()
    else:
        print_ascii_layout()
        print_block_reference()
