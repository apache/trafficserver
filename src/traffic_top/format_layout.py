#!/usr/bin/env python3
"""
Format traffic_top layout lines with exact character widths.

Each 40-char box format:
| Label       Value   Label       Value |

Stat 1: 17 chars (label + spaces + value)
Gap: 3 spaces
Stat 2: 16 chars (label + spaces + value)
Padding: 1 space each side
Total: 1 + 1 + 17 + 3 + 16 + 1 + 1 = 40
"""


def format_stat(label: str, value: str, width: int) -> str:
    """Format a single stat (label + value) to exact width.

    Numbers are right-aligned at a fixed position (width - 1).
    Suffix follows the number. Values without suffix get trailing space.
    """
    value_str = str(value)
    label_str = str(label)

    # Separate number from suffix (check longer suffixes first)
    suffix = ""
    num_str = value_str
    for s in ["ms", "%", "K", "M", "G", "T", "d"]:
        if value_str.endswith(s):
            suffix = s
            num_str = value_str[:-len(s)]
            break

    # Always reserve 1 char for suffix position so numbers align
    # Values without suffix get a trailing space
    suffix_field = 1
    actual_suffix_len = len(suffix)

    # For 2-char suffix like "ms", we need more space
    if actual_suffix_len > 1:
        suffix_field = actual_suffix_len

    # Calculate number field width
    available_for_num = width - len(label_str) - 1 - suffix_field

    if available_for_num < len(num_str):
        # Truncate label if needed
        label_str = label_str[:width - len(num_str) - 1 - suffix_field]
        available_for_num = width - len(label_str) - 1 - suffix_field

    # Right-align the number in its field
    num_field = num_str.rjust(available_for_num)

    # Build result - pad suffix to suffix_field width
    if actual_suffix_len == 0:
        suffix_part = " "  # trailing space where suffix would be
    else:
        suffix_part = suffix

    return f"{label_str} {num_field}{suffix_part}"


def format_box_line(stats: list, box_width: int = 40) -> str:
    """Format a line inside a box with 2 stat pairs."""
    content_width = box_width - 4  # 36 for 40-char box
    stat1_width = 17
    gap = 3
    stat2_width = content_width - stat1_width - gap  # 16

    stat1 = format_stat(stats[0][0], stats[0][1], stat1_width)
    stat2 = format_stat(stats[1][0], stats[1][1], stat2_width)

    return f"| {stat1}{' ' * gap}{stat2} |"


def format_multi_box_line(all_stats: list, num_boxes: int, box_width: int = 40) -> str:
    """Format a line with multiple boxes side by side."""
    boxes = [format_box_line(stats, box_width) for stats in all_stats]
    line = "||".join(b[1:-1] for b in boxes)
    return "|" + line + "|"


def format_header(title: str, box_width: int = 40) -> str:
    """Format a box header like '+--- TITLE ---+'"""
    content_width = box_width - 2
    title_with_spaces = f" {title} "
    dashes_needed = content_width - len(title_with_spaces)
    left_dashes = dashes_needed // 2
    right_dashes = dashes_needed - left_dashes
    return f"+{'-' * left_dashes}{title_with_spaces}{'-' * right_dashes}+"


def format_separator(box_width: int = 40) -> str:
    """Format a box separator like '+----...----+'"""
    return f"+{'-' * (box_width - 2)}+"


def multi_header(titles: list) -> str:
    """Format multiple headers joined together."""
    return "".join(format_header(t) for t in titles)


def multi_separator(num_boxes: int) -> str:
    """Format multiple separators joined together."""
    return "".join(format_separator() for _ in range(num_boxes))


def generate_80x24():
    """Generate the 80x24 layout."""
    print("## 80x24 Terminal (2 boxes)")
    print()
    print("```")

    # Row 1: CACHE, REQS/RESPONSES
    print(multi_header(["CACHE", "REQS/RESPONSES"]))

    rows = [
        [[("Disk Used", "120G"), ("RAM Used", "512M")], [("GET", "15K"), ("POST", "800")]],
        [[("Disk Total", "500G"), ("RAM Total", "1G")], [("HEAD", "200"), ("PUT", "50")]],
        [[("RAM Hit", "85%"), ("Fresh", "72%")], [("DELETE", "10"), ("OPTIONS", "25")]],
        [[("Revalidate", "12%"), ("Cold", "8%")], [("200", "78%"), ("206", "5%")]],
        [[("Changed", "3%"), ("Not Cached", "2%")], [("301", "2%"), ("304", "12%")]],
        [[("No Cache", "3%"), ("Entries", "50K")], [("404", "1%"), ("502", "0%")]],
        [[("Lookups", "25K"), ("Writes", "8K")], [("2xx", "83%"), ("3xx", "14%")]],
        [[("Read Active", "150"), ("Write Act", "45")], [("4xx", "2%"), ("5xx", "1%")]],
        [[("Updates", "500"), ("Deletes", "100")], [("Error", "15"), ("Other Err", "3")]],
    ]
    for row in rows:
        print(format_multi_box_line(row, 2))
    print(multi_separator(2))

    # Row 2: CLIENT, ORIGIN
    print(multi_header(["CLIENT", "ORIGIN"]))

    rows = [
        [[("Requests", "15K"), ("Connections", "800")], [("Requests", "12K"), ("Connections", "400")]],
        [[("Current Conn", "500"), ("Active Conn", "450")], [("Current Conn", "200"), ("Req/Conn", "30")]],
        [[("Req/Conn", "19"), ("Dynamic KA", "400")], [("Connect Fail", "5"), ("Aborts", "2")]],
        [[("Avg Size", "45K"), ("Net (Mb/s)", "850")], [("Avg Size", "52K"), ("Net (Mb/s)", "620")]],
        [[("Resp Time", "12"), ("Head Bytes", "18M")], [("Keep Alive", "380"), ("Conn Reuse", "350")]],
        [[("Body Bytes", "750M"), ("HTTP/1 Conn", "200")], [("Head Bytes", "15M"), ("Body Bytes", "600M")]],
        [[("HTTP/2 Conn", "300"), ("SSL Session", "450")], [("DNS Lookups", "800"), ("DNS Hits", "720")]],
        [[("SSL Handshk", "120"), ("SSL Errors", "3")], [("DNS Ratio", "90%"), ("DNS Entry", "500")]],
        [[("Hit Latency", "2"), ("Miss Laten", "45")], [("Error", "12"), ("Other Err", "5")]],
    ]
    for row in rows:
        print(format_multi_box_line(row, 2))
    print(multi_separator(2))

    print(" 12:30:45  proxy.example.com            [1/6] Overview               q h 1-6")
    print("```")


def generate_120x40():
    """Generate the 120x40 layout."""
    print("## 120x40 Terminal (3 boxes)")
    print()
    print("```")

    # Row 1: CACHE, REQUESTS, CONNECTIONS
    print(multi_header(["CACHE", "REQUESTS", "CONNECTIONS"]))
    rows = [
        [
            [("Disk Used", "120G"), ("Disk Total", "500G")], [("Client Req", "15K"), ("Server Req", "12K")],
            [("Client Conn", "800"), ("Current", "500")]
        ],
        [
            [("RAM Used", "512M"), ("RAM Total", "1G")], [("GET", "12K"), ("POST", "800")],
            [("Active Conn", "450"), ("Server Con", "400")]
        ],
        [
            [("RAM Ratio", "85%"), ("Entries", "50K")], [("HEAD", "200"), ("PUT", "50")],
            [("Server Curr", "200"), ("Req/Conn", "30")]
        ],
        [
            [("Lookups", "25K"), ("Writes", "8K")], [("DELETE", "10"), ("OPTIONS", "25")],
            [("HTTP/1 Conn", "200"), ("HTTP/2", "300")]
        ],
        [
            [("Read Active", "150"), ("Write Act", "45")], [("PURGE", "5"), ("PUSH", "2")],
            [("Keep Alive", "380"), ("Conn Reuse", "350")]
        ],
        [
            [("Updates", "500"), ("Deletes", "100")], [("CONNECT", "15"), ("TRACE", "0")],
            [("Dynamic KA", "400"), ("Throttled", "5")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 3))
    print(multi_separator(3))

    # Row 2: HIT RATES, RESPONSES, BANDWIDTH
    print(multi_header(["HIT RATES", "RESPONSES", "BANDWIDTH"]))
    rows = [
        [[("RAM Hit", "85%"), ("Fresh", "72%")], [("200", "78%"), ("206", "5%")], [("Client Head", "18M"), ("Client Bod", "750M")]],
        [
            [("Revalidate", "12%"), ("Cold", "8%")], [("301", "2%"), ("304", "12%")],
            [("Server Head", "15M"), ("Server Bod", "600M")]
        ],
        [[("Changed", "3%"), ("Not Cached", "2%")], [("404", "1%"), ("502", "0%")], [("Avg ReqSize", "45K"), ("Avg Resp", "52K")]],
        [[("No Cache", "3%"), ("Error", "1%")], [("503", "0%"), ("504", "0%")], [("Net In Mbs", "850"), ("Net Out", "620")]],
        [
            [("Fresh Time", "2ms"), ("Reval Time", "15")], [("2xx", "83%"), ("3xx", "14%")],
            [("Head Bytes", "33M"), ("Body Bytes", "1G")]
        ],
        [
            [("Cold Time", "45"), ("Changed T", "30")], [("4xx", "2%"), ("5xx", "1%")],
            [("Avg Latency", "12ms"), ("Max Laten", "450")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 3))
    print(multi_separator(3))

    # Row 3: SSL/TLS, DNS, ERRORS
    print(multi_header(["SSL/TLS", "DNS", "ERRORS"]))
    rows = [
        [
            [("SSL Success", "450"), ("SSL Fail", "3")], [("DNS Lookups", "800"), ("DNS Hits", "720")],
            [("Connect Fail", "5"), ("Aborts", "2")]
        ],
        [
            [("SSL Session", "450"), ("SSL Handshk", "120")], [("DNS Ratio", "90%"), ("DNS Entry", "500")],
            [("Client Abrt", "15"), ("Origin Err", "12")]
        ],
        [
            [("Session Hit", "400"), ("Session Mis", "50")], [("Pending", "5"), ("In Flight", "12")],
            [("CacheRdErr", "3"), ("Cache Writ", "1")]
        ],
        [[("TLS 1.2", "200"), ("TLS 1.3", "250")], [("Expired", "10"), ("Evicted", "25")], [("Timeout", "20"), ("Other Err", "8")]],
        [
            [("Client Cert", "50"), ("Origin SSL", "380")], [("Avg Lookup", "2ms"), ("Max Lookup", "45")],
            [("HTTP Err", "10"), ("Parse Err", "2")]
        ],
        [
            [("Renegotiate", "10"), ("Resumption", "350")], [("Failed", "5"), ("Retries", "12")],
            [("DNS Fail", "5"), ("SSL Err", "3")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 3))
    print(multi_separator(3))

    # Row 4: CLIENT, ORIGIN, TOTALS
    print(multi_header(["CLIENT", "ORIGIN", "TOTALS"]))
    rows = [
        [
            [("Requests", "15K"), ("Connections", "800")], [("Requests", "12K"), ("Connections", "400")],
            [("Total Req", "150M"), ("Total Conn", "5M")]
        ],
        [
            [("Current Con", "500"), ("Active Conn", "450")], [("Current Con", "200"), ("Req/Conn", "30")],
            [("Total Bytes", "50T"), ("Uptime", "45d")]
        ],
        [
            [("Avg Size", "45K"), ("Net (Mb/s)", "850")], [("Avg Size", "52K"), ("Net (Mb/s)", "620")],
            [("Cache Size", "120G"), ("RAM Cache", "512M")]
        ],
        [
            [("Resp Time", "12"), ("Head Bytes", "18M")], [("Keep Alive", "380"), ("Conn Reuse", "350")],
            [("Hit Rate", "85%"), ("Bandwidth", "850M")]
        ],
        [
            [("Body Bytes", "750M"), ("Errors", "15")], [("Head Bytes", "15M"), ("Body Bytes", "600M")],
            [("Avg Resp", "12ms"), ("Peak Req", "25K")]
        ],
        [
            [("HTTP/1 Conn", "300"), ("HTTP/2 Con", "300")], [("Errors", "12"), ("Other Err", "5")],
            [("Errors/hr", "50"), ("Uptime %", "99%")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 3))
    print(multi_separator(3))

    # Row 5: HTTP CODES, CACHE DETAIL, SYSTEM
    print(multi_header(["HTTP CODES", "CACHE DETAIL", "SYSTEM"]))
    rows = [
        [
            [("100", "0"), ("101", "0")], [("Lookup Act", "150"), ("Lookup Suc", "24K")],
            [("Thread Cnt", "32"), ("Event Loop", "16")]
        ],
        [
            [("200", "78%"), ("201", "1%")], [("Read Active", "150"), ("Read Succ", "20K")],
            [("Memory Use", "2.5G"), ("Peak Mem", "3G")]
        ],
        [[("204", "2%"), ("206", "5%")], [("Write Act", "45"), ("Write Succ", "8K")], [("Open FDs", "5K"), ("Max FDs", "64K")]],
        [
            [("301", "2%"), ("302", "1%")], [("Update Act", "10"), ("Update Suc", "500")],
            [("CPU User", "45%"), ("CPU System", "15%")]
        ],
        [
            [("304", "12%"), ("307", "0%")], [("Delete Act", "5"), ("Delete Suc", "100")],
            [("IO Read", "850M"), ("IO Write", "620M")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 3))
    print(multi_separator(3))

    print(
        " 12:30:45  proxy.example.com                           [1/3] Overview                                            q h 1-3")
    print("```")


def generate_160x40():
    """Generate the 160x40 layout."""
    print("## 160x40 Terminal (4 boxes)")
    print()
    print("```")

    # Row 1: CACHE, CLIENT, ORIGIN, REQUESTS
    print(multi_header(["CACHE", "CLIENT", "ORIGIN", "REQUESTS"]))
    rows = [
        [
            [("Disk Used", "120G"), ("Disk Total", "500G")], [("Requests", "15K"), ("Connections", "800")],
            [("Requests", "12K"), ("Connections", "400")], [("GET", "12K"), ("POST", "800")]
        ],
        [
            [("RAM Used", "512M"), ("RAM Total", "1G")], [("Current Con", "500"), ("Active Conn", "450")],
            [("Current Con", "200"), ("Req/Conn", "30")], [("HEAD", "200"), ("PUT", "50")]
        ],
        [
            [("Entries", "50K"), ("Avg Size", "45K")], [("Req/Conn", "19"), ("Dynamic KA", "400")],
            [("Connect Fai", "5"), ("Aborts", "2")], [("DELETE", "10"), ("OPTIONS", "25")]
        ],
        [
            [("Lookups", "25K"), ("Writes", "8K")], [("Avg Size", "45K"), ("Net (Mb/s)", "850")],
            [("Avg Size", "52K"), ("Net (Mb/s)", "620")], [("PURGE", "5"), ("PUSH", "2")]
        ],
        [
            [("Read Active", "150"), ("Write Act", "45")], [("Resp Time", "12"), ("Head Bytes", "18M")],
            [("Keep Alive", "380"), ("Conn Reuse", "350")], [("CONNECT", "15"), ("TRACE", "0")]
        ],
        [
            [("Updates", "500"), ("Deletes", "100")], [("Body Bytes", "750M"), ("Errors", "15")],
            [("Head Bytes", "15M"), ("Body Bytes", "600M")], [("Total Req", "150M"), ("Req/sec", "15K")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 4))
    print(multi_separator(4))

    # Row 2: HIT RATES, CONNECTIONS, SSL/TLS, RESPONSES
    print(multi_header(["HIT RATES", "CONNECTIONS", "SSL/TLS", "RESPONSES"]))
    rows = [
        [
            [("RAM Hit", "85%"), ("Fresh", "72%")], [("HTTP/1 Clnt", "200"), ("HTTP/1 Orig", "80")],
            [("SSL Success", "450"), ("SSL Fail", "3")], [("200", "78%"), ("206", "5%")]
        ],
        [
            [("Revalidate", "12%"), ("Cold", "8%")], [("HTTP/2 Clnt", "300"), ("HTTP/2 Orig", "120")],
            [("SSL Session", "450"), ("SSL Handshk", "120")], [("301", "2%"), ("304", "12%")]
        ],
        [
            [("Changed", "3%"), ("Not Cached", "2%")], [("HTTP/3 Clnt", "50"), ("HTTP/3 Orig", "20")],
            [("Session Hit", "400"), ("Session Mis", "50")], [("404", "1%"), ("502", "0%")]
        ],
        [
            [("No Cache", "3%"), ("Error", "1%")], [("Keep Alive", "380"), ("Conn Reuse", "350")],
            [("TLS 1.2", "200"), ("TLS 1.3", "250")], [("503", "0%"), ("504", "0%")]
        ],
        [
            [("Fresh Time", "2ms"), ("Reval Time", "15")], [("Throttled", "5"), ("Queued", "2")],
            [("Client Cert", "50"), ("Origin SSL", "380")], [("2xx", "83%"), ("3xx", "14%")]
        ],
        [
            [("Cold Time", "45"), ("Changed T", "30")], [("Idle Timeou", "10"), ("Max Conns", "5K")],
            [("Renegotiate", "10"), ("Resumption", "350")], [("4xx", "2%"), ("5xx", "1%")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 4))
    print(multi_separator(4))

    # Row 3: BANDWIDTH, DNS, ERRORS, TOTALS
    print(multi_header(["BANDWIDTH", "DNS", "ERRORS", "TOTALS"]))
    rows = [
        [
            [("Client Head", "18M"), ("Client Bod", "750M")], [("DNS Lookups", "800"), ("DNS Hits", "720")],
            [("Connect Fai", "5"), ("Aborts", "2")], [("Total Req", "150M"), ("Total Conn", "5M")]
        ],
        [
            [("Server Head", "15M"), ("Server Bod", "600M")], [("DNS Ratio", "90%"), ("DNS Entry", "500")],
            [("Client Abrt", "15"), ("Origin Err", "12")], [("Total Bytes", "50T"), ("Uptime", "45d")]
        ],
        [
            [("Avg ReqSize", "45K"), ("Avg Resp", "52K")], [("Pending", "5"), ("In Flight", "12")],
            [("CacheRdErr", "3"), ("Cache Writ", "1")], [("Cache Size", "120G"), ("RAM Cache", "512M")]
        ],
        [
            [("Net In Mbs", "850"), ("Net Out", "620")], [("Expired", "10"), ("Evicted", "25")],
            [("Timeout", "20"), ("Other Err", "8")], [("Hit Rate", "85%"), ("Bandwidth", "850M")]
        ],
        [
            [("Head Bytes", "33M"), ("Body Bytes", "1G")], [("Avg Lookup", "2ms"), ("Max Lookup", "45")],
            [("HTTP Err", "10"), ("Parse Err", "2")], [("Avg Resp", "12ms"), ("Peak Req", "25K")]
        ],
        [
            [("Avg Latency", "12ms"), ("Max Laten", "450")], [("Failed", "5"), ("Retries", "12")],
            [("DNS Fail", "5"), ("SSL Err", "3")], [("Errors/hr", "50"), ("Uptime %", "99%")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 4))
    print(multi_separator(4))

    # Row 4: HTTP CODES, CACHE DETAIL, ORIGIN DETAIL, MISC STATS
    print(multi_header(["HTTP CODES", "CACHE DETAIL", "ORIGIN DETAIL", "MISC STATS"]))
    rows = [
        [
            [("100", "0"), ("101", "0")], [("Lookup Act", "150"), ("Lookup Suc", "24K")],
            [("Req Active", "50"), ("Req Pending", "12")], [("Thread Cnt", "32"), ("Event Loop", "16")]
        ],
        [
            [("200", "78%"), ("201", "1%")], [("Read Active", "150"), ("Read Succ", "20K")],
            [("Conn Active", "200"), ("Conn Pend", "25")], [("Memory Use", "2.5G"), ("Peak Mem", "3G")]
        ],
        [
            [("204", "2%"), ("206", "5%")], [("Write Act", "45"), ("Write Succ", "8K")],
            [("DNS Pending", "5"), ("DNS Active", "12")], [("Open FDs", "5K"), ("Max FDs", "64K")]
        ],
        [
            [("301", "2%"), ("302", "1%")], [("Update Act", "10"), ("Update Suc", "500")],
            [("SSL Active", "50"), ("SSL Pend", "10")], [("CPU User", "45%"), ("CPU System", "15%")]
        ],
        [
            [("304", "12%"), ("307", "0%")], [("Delete Act", "5"), ("Delete Suc", "100")],
            [("Retry Queue", "10"), ("Retry Act", "5")], [("IO Read", "850M"), ("IO Write", "620M")]
        ],
        [
            [("400", "1%"), ("401", "0%")], [("Evacuate", "5"), ("Scan", "2")], [("Timeout Que", "5"), ("Timeout Act", "2")],
            [("Net Pkts", "100K"), ("Dropped", "50")]
        ],
        [
            [("403", "0%"), ("404", "1%")], [("Fragment 1", "15K"), ("Fragment 2", "3K")],
            [("Error Queue", "5"), ("Error Act", "2")], [("Ctx Switch", "50K"), ("Interrupts", "25K")]
        ],
        [
            [("500", "0%"), ("502", "0%")], [("Fragment 3+", "500"), ("Avg Frags", "1.2")],
            [("Health Chk", "100"), ("Health OK", "98")], [("GC Runs", "100"), ("GC Time", "50ms")]
        ],
        [
            [("503", "0%"), ("504", "0%")], [("Bytes Writ", "50T"), ("Bytes Read", "45T")],
            [("Circuit Opn", "0"), ("Circuit Cls", "5")], [("Log Writes", "10K"), ("Log Bytes", "500M")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 4))
    print(multi_separator(4))

    # Row 5: PROTOCOLS, TIMEOUTS, QUEUES, RESOURCES
    print(multi_header(["PROTOCOLS", "TIMEOUTS", "QUEUES", "RESOURCES"]))
    rows = [
        [
            [("HTTP/1.0", "50"), ("HTTP/1.1", "150")], [("Connect TO", "10"), ("Read TO", "5")],
            [("Accept Queu", "25"), ("Active Q", "50")], [("Threads Idl", "16"), ("Threads Bu", "16")]
        ],
        [
            [("HTTP/2", "300"), ("HTTP/3", "50")], [("Write TO", "3"), ("DNS TO", "2")], [("Pending Q", "12"), ("Retry Q", "5")],
            [("Disk Free", "380G"), ("Disk Used", "120G")]
        ],
    ]
    for row in rows:
        print(format_multi_box_line(row, 4))
    print(multi_separator(4))

    print(
        " 12:30:45  proxy.example.com                                                   [1/2] Overview                                                        q h 1-2"
    )
    print("```")


if __name__ == "__main__":
    generate_80x24()
    print()
    generate_120x40()
    print()
    generate_160x40()
