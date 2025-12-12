# traffic_top Layout Documentation

This document shows the exact layouts for different terminal sizes.
All layouts use ASCII characters and are exactly the width specified.

## Column Format

Each 40-character box contains two stat columns:

```
| Disk Used    120G   RAM Used    512M |
```

- **Box width**: 40 characters total (including `|` borders)
- **Content width**: 38 characters inside borders
- **Stat 1**: 17 characters (label + spaces + number + suffix)
- **Column gap**: 3 spaces between stat pairs
- **Stat 2**: 16 characters (label + spaces + number + suffix)
- **Padding**: 1 space after `|` and 1 space before `|`
- **Numbers are right-aligned** at a fixed position
- **Suffix follows the number** (%, K, M, G, T attached to number)
- **Values without suffix** have trailing space to maintain alignment
- **Labels and values never touch** - always at least 1 space between

Breakdown: `| ` (2) + stat1 (17) + gap (3) + stat2 (16) + ` |` (2) = 40 ✓

## 80x24 Terminal (2 boxes)

```
+--------------- CLIENT ---------------++--------------- ORIGIN ---------------+
| Requests      15K   Connections 800  || Requests      12K   Connections 400  |
| Current Conn 500    Active Conn 450  || Current Conn 200    Req/Conn     30  |
| Req/Conn      19    Dynamic KA  400  || Connect Fail   5    Aborts        2  |
| Avg Size      45K   Net (Mb/s)  850  || Avg Size      52K   Net (Mb/s)  620  |
| Resp Time     12    Head Bytes   18M || Keep Alive   380    Conn Reuse  350  |
| Body Bytes   750M   HTTP/1 Conn 200  || Head Bytes    15M   Body Bytes  600M |
| HTTP/2 Conn  300    SSL Session 450  || DNS Lookups  800    DNS Hits    720  |
| SSL Handshk  120    SSL Errors    3  || DNS Ratio     90%   DNS Entry   500  |
| Hit Latency    2    Miss Laten   45  || Error         12    Other Err     5  |
+--------------------------------------++--------------------------------------+
+--------------- CACHE ----------------++----------- REQS/RESPONSES -----------+
| Disk Used    120G   RAM Used    512M || GET           15K   POST        800  |
| Disk Total   500G   RAM Total     1G || HEAD         200    PUT          50  |
| RAM Hit       85%   Fresh        72% || DELETE        10    OPTIONS      25  |
| Revalidate    12%   Cold          8% || 200           78%   206           5% |
| Changed        3%   Not Cached    2% || 301            2%   304          12% |
| No Cache       3%   Entries      50K || 404            1%   502           0% |
| Lookups       25K   Writes        8K || 2xx           83%   3xx          14% |
| Read Act     150    Write Act    45  || 4xx            2%   5xx           1% |
| Updates      500    Deletes     100  || Error         15    Other Err     3  |
+--------------------------------------++--------------------------------------+
 12:30:45  proxy.example.com            [1/6] Overview               q h 1-6
```

## 120x40 Terminal (3 boxes)

```
+--------------- CACHE ----------------++-------------- REQUESTS --------------++------------ CONNECTIONS -------------+
| Disk Used    120G   Disk Total  500G || Client Req    15K   Server Req   12K || Client Conn  800    Current     500  |
| RAM Used     512M   RAM Total     1G || GET           12K   POST        800  || Active Conn  450    Server Con  400  |
| RAM Ratio     85%   Entries      50K || HEAD         200    PUT          50  || Server Curr  200    Req/Conn     30  |
| Lookups       25K   Writes        8K || DELETE        10    OPTIONS      25  || HTTP/1 Conn  200    HTTP/2      300  |
| Read Active  150    Write Act    45  || PURGE          5    PUSH          2  || Keep Alive   380    Conn Reuse  350  |
| Updates      500    Deletes     100  || CONNECT       15    TRACE         0  || Dynamic KA   400    Throttled     5  |
+--------------------------------------++--------------------------------------++--------------------------------------+
+------------- HIT RATES --------------++------------- RESPONSES --------------++------------- BANDWIDTH --------------+
| RAM Hit       85%   Fresh        72% || 200           78%   206           5% || Client Head   18M   Client Bod  750M |
| Revalidate    12%   Cold          8% || 301            2%   304          12% || Server Head   15M   Server Bod  600M |
| Changed        3%   Not Cached    2% || 404            1%   502           0% || Avg ReqSize   45K   Avg Resp     52K |
| No Cache       3%   Error         1% || 503            0%   504           0% || Net In Mbs   850    Net Out     620  |
| Fresh Time    2ms   Reval Time   15  || 2xx           83%   3xx          14% || Head Bytes    33M   Body Bytes    1G |
| Cold Time     45    Changed T    30  || 4xx            2%   5xx           1% || Avg Latency  12ms   Max Laten   450  |
+--------------------------------------++--------------------------------------++--------------------------------------+
+-------------- SSL/TLS ---------------++---------------- DNS -----------------++--------------- ERRORS ---------------+
| SSL Success  450    SSL Fail      3  || DNS Lookups  800    DNS Hits    720  || Connect Fail   5    Aborts        2  |
| SSL Session  450    SSL Handshk 120  || DNS Ratio     90%   DNS Entry   500  || Client Abrt   15    Origin Err   12  |
| Session Hit  400    Session Mis  50  || Pending        5    In Flight    12  || CacheRdErr     3    Cache Writ    1  |
| TLS 1.2      200    TLS 1.3     250  || Expired       10    Evicted      25  || Timeout       20    Other Err     8  |
| Client Cert   50    Origin SSL  380  || Avg Lookup    2ms   Max Lookup   45  || HTTP Err      10    Parse Err     2  |
| Renegotiate   10    Resumption  350  || Failed         5    Retries      12  || DNS Fail       5    SSL Err       3  |
+--------------------------------------++--------------------------------------++--------------------------------------+
+--------------- CLIENT ---------------++--------------- ORIGIN ---------------++--------------- TOTALS ---------------+
| Requests      15K   Connections 800  || Requests      12K   Connections 400  || Total Req    150M   Total Conn    5M |
| Current Con  500    Active Conn 450  || Current Con  200    Req/Conn     30  || Total Bytes   50T   Uptime       45d |
| Avg Size      45K   Net (Mb/s)  850  || Avg Size      52K   Net (Mb/s)  620  || Cache Size   120G   RAM Cache   512M |
| Resp Time     12    Head Bytes   18M || Keep Alive   380    Conn Reuse  350  || Hit Rate      85%   Bandwidth   850M |
| Body Bytes   750M   Errors       15  || Head Bytes    15M   Body Bytes  600M || Avg Resp     12ms   Peak Req     25K |
| HTTP/1 Conn  300    HTTP/2 Con  300  || Errors        12    Other Err     5  || Errors/hr     50    Uptime %     99% |
+--------------------------------------++--------------------------------------++--------------------------------------+
+------------- HTTP CODES -------------++------------ CACHE DETAIL ------------++--------------- SYSTEM ---------------+
| 100            0    101           0  || Lookup Act   150    Lookup Suc   24K || Thread Cnt    32    Event Loop   16  |
| 200           78%   201           1% || Read Active  150    Read Succ    20K || Memory Use   2.5G   Peak Mem      3G |
| 204            2%   206           5% || Write Act     45    Write Succ    8K || Open FDs       5K   Max FDs      64K |
| 301            2%   302           1% || Update Act    10    Update Suc  500  || CPU User      45%   CPU System   15% |
| 304           12%   307           0% || Delete Act     5    Delete Suc  100  || IO Read      850M   IO Write    620M |
+--------------------------------------++--------------------------------------++--------------------------------------+
 12:30:45  proxy.example.com                           [1/3] Overview                                            q h 1-3
```

## 160x40 Terminal (4 boxes)

```
+--------------- CACHE ----------------++--------------- CLIENT ---------------++--------------- ORIGIN ---------------++-------------- REQUESTS --------------+
| Disk Used    120G   Disk Total  500G || Requests      15K   Connections 800  || Requests      12K   Connections 400  || GET           12K   POST        800  |
| RAM Used     512M   RAM Total     1G || Current Con  500    Active Conn 450  || Current Con  200    Req/Conn     30  || HEAD         200    PUT          50  |
| Entries       50K   Avg Size     45K || Req/Conn      19    Dynamic KA  400  || Connect Fai    5    Aborts        2  || DELETE        10    OPTIONS      25  |
| Lookups       25K   Writes        8K || Avg Size      45K   Net (Mb/s)  850  || Avg Size      52K   Net (Mb/s)  620  || PURGE          5    PUSH          2  |
| Read Active  150    Write Act    45  || Resp Time     12    Head Bytes   18M || Keep Alive   380    Conn Reuse  350  || CONNECT       15    TRACE         0  |
| Updates      500    Deletes     100  || Body Bytes   750M   Errors       15  || Head Bytes    15M   Body Bytes  600M || Total Req    150M   Req/sec      15K |
+--------------------------------------++--------------------------------------++--------------------------------------++--------------------------------------+
+------------- HIT RATES --------------++------------ CONNECTIONS -------------++-------------- SSL/TLS ---------------++------------- RESPONSES --------------+
| RAM Hit       85%   Fresh        72% || HTTP/1 Clnt  200    HTTP/1 Orig  80  || SSL Success  450    SSL Fail      3  || 200           78%   206           5% |
| Revalidate    12%   Cold          8% || HTTP/2 Clnt  300    HTTP/2 Orig 120  || SSL Session  450    SSL Handshk 120  || 301            2%   304          12% |
| Changed        3%   Not Cached    2% || HTTP/3 Clnt   50    HTTP/3 Orig  20  || Session Hit  400    Session Mis  50  || 404            1%   502           0% |
| No Cache       3%   Error         1% || Keep Alive   380    Conn Reuse  350  || TLS 1.2      200    TLS 1.3     250  || 503            0%   504           0% |
| Fresh Time    2ms   Reval Time   15  || Throttled      5    Queued        2  || Client Cert   50    Origin SSL  380  || 2xx           83%   3xx          14% |
| Cold Time     45    Changed T    30  || Idle Timeou   10    Max Conns     5K || Renegotiate   10    Resumption  350  || 4xx            2%   5xx           1% |
+--------------------------------------++--------------------------------------++--------------------------------------++--------------------------------------+
+------------- BANDWIDTH --------------++---------------- DNS -----------------++--------------- ERRORS ---------------++--------------- TOTALS ---------------+
| Client Head   18M   Client Bod  750M || DNS Lookups  800    DNS Hits    720  || Connect Fai    5    Aborts        2  || Total Req    150M   Total Conn    5M |
| Server Head   15M   Server Bod  600M || DNS Ratio     90%   DNS Entry   500  || Client Abrt   15    Origin Err   12  || Total Bytes   50T   Uptime       45d |
| Avg ReqSize   45K   Avg Resp     52K || Pending        5    In Flight    12  || CacheRdErr     3    Cache Writ    1  || Cache Size   120G   RAM Cache   512M |
| Net In Mbs   850    Net Out     620  || Expired       10    Evicted      25  || Timeout       20    Other Err     8  || Hit Rate      85%   Bandwidth   850M |
| Head Bytes    33M   Body Bytes    1G || Avg Lookup    2ms   Max Lookup   45  || HTTP Err      10    Parse Err     2  || Avg Resp     12ms   Peak Req     25K |
| Avg Latency  12ms   Max Laten   450  || Failed         5    Retries      12  || DNS Fail       5    SSL Err       3  || Errors/hr     50    Uptime %     99% |
+--------------------------------------++--------------------------------------++--------------------------------------++--------------------------------------+
+------------- HTTP CODES -------------++------------ CACHE DETAIL ------------++----------- ORIGIN DETAIL ------------++------------- MISC STATS -------------+
| 100            0    101           0  || Lookup Act   150    Lookup Suc   24K || Req Active    50    Req Pending  12  || Thread Cnt    32    Event Loop   16  |
| 200           78%   201           1% || Read Active  150    Read Succ    20K || Conn Active  200    Conn Pend    25  || Memory Use   2.5G   Peak Mem      3G |
| 204            2%   206           5% || Write Act     45    Write Succ    8K || DNS Pending    5    DNS Active   12  || Open FDs       5K   Max FDs      64K |
| 301            2%   302           1% || Update Act    10    Update Suc  500  || SSL Active    50    SSL Pend     10  || CPU User      45%   CPU System   15% |
| 304           12%   307           0% || Delete Act     5    Delete Suc  100  || Retry Queue   10    Retry Act     5  || IO Read      850M   IO Write    620M |
| 400            1%   401           0% || Evacuate       5    Scan          2  || Timeout Que    5    Timeout Act   2  || Net Pkts     100K   Dropped      50  |
| 403            0%   404           1% || Fragment 1    15K   Fragment 2    3K || Error Queue    5    Error Act     2  || Ctx Switch    50K   Interrupts   25K |
| 500            0%   502           0% || Fragment 3+  500    Avg Frags   1.2  || Health Chk   100    Health OK    98  || GC Runs      100    GC Time     50ms |
| 503            0%   504           0% || Bytes Writ    50T   Bytes Read   45T || Circuit Opn    0    Circuit Cls   5  || Log Writes    10K   Log Bytes   500M |
+--------------------------------------++--------------------------------------++--------------------------------------++--------------------------------------+
+------------- PROTOCOLS --------------++-------------- TIMEOUTS --------------++--------------- QUEUES ---------------++------------- RESOURCES --------------+
| HTTP/1.0      50    HTTP/1.1    150  || Connect TO    10    Read TO       5  || Accept Queu   25    Active Q     50  || Threads Idl   16    Threads Bu   16  |
| HTTP/2       300    HTTP/3       50  || Write TO       3    DNS TO        2  || Pending Q     12    Retry Q       5  || Disk Free    380G   Disk Used   120G |
+--------------------------------------++--------------------------------------++--------------------------------------++--------------------------------------+
 12:30:45  proxy.example.com                                                   [1/2] Overview                                                        q h 1-2
```

## Page Layouts

Page count varies by terminal size due to available space:

### 80x24 Terminal (6 Pages)
1. **Overview** - Cache, Reqs/Responses, Client, Origin
2. **Responses** - HTTP response code breakdown (1xx, 2xx, 3xx, 4xx, 5xx)
3. **Connections** - HTTP/1.x vs HTTP/2, keep-alive, bandwidth
4. **Cache** - Detailed cache statistics, hit rates, latency
5. **SSL/TLS** - SSL handshake stats, session cache, errors
6. **Errors** - Connection, transaction, cache, origin errors

### 120x40 Terminal (3 Pages)
1. **Overview** - All major operational stats (shown above)
2. **Details** - HTTP codes, responses, SSL/TLS, DNS, errors combined
3. **System** - Cache detail, system resources, timeouts, totals

### 160x40 Terminal (2 Pages)
1. **Overview** - All major operational stats (shown above)
2. **Details** - Deep dives into HTTP codes, cache internals, system

## Status Bar

The status bar appears on the last line and contains:
- Timestamp (HH:MM:SS)
- Connection status indicator
- Hostname
- Current page indicator [N/X] where X = 6, 3, or 2 based on terminal size
- Key hints (q h 1-X)

## Color Scheme (Interactive Mode)

- Box borders: Cyan, Blue, Magenta (alternating)
- Labels: Cyan
- Values: Color-coded by magnitude
  - Grey: Zero or very small values
  - Green: Normal values
  - Cyan: Thousands (K suffix)
  - Yellow: Millions (M suffix)
  - Red: Billions (G suffix)
- Percentages: Green (>90%), Cyan (>70%), Yellow (>50%), Grey (<1%)

## Notes

- Values are formatted with SI suffixes (K, M, G, T)
- Percentages show as integer with % suffix
- Numbers are right-aligned, suffix follows immediately
- Values without suffix have trailing space for alignment
- Unicode box-drawing characters used by default
- Use -a flag for ASCII box characters (+, -, |)

## Graph Page Layouts

Graphs use Unicode block characters for btop-style visualization:
`▁▂▃▄▅▆▇█` (8 height levels from 0% to 100%)

### Multi-Graph Box Format

Title and graphs inside the box allow multiple metrics per box:

```
| LABEL        ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂  VALUE |
```

### 40-char Multi-Graph Box

```
+--------------------------------------+
| Bandwidth    ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂ 850M |
| Hit Rate      ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂ 85% |
| Requests      ▂▂▄▄▄▃▂▇▂▇▆▂▂▂▃▄▆▇ 15K |
| Connections    ▁▁▂▂▃▃▄▄▅▅▅▆▆▇▇██ 800 |
+--------------------------------------+
```

### 80x24 Graph Page (two 40-char boxes + 80-char box)

```
+-------------- NETWORK ---------------++------------- CACHE I/O --------------+
| Net In       ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂ 850M || Reads         ▂▂▄▄▄▃▂▇▂▇▆▂▂▂▃▄▆▇ 25K |
| Net Out      ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂ 620M || Writes          ▁▁▂▂▃▃▄▄▅▅▅▆▆▇▇██ 8K |
+--------------------------------------++--------------------------------------+
+------------------------------ TRAFFIC OVERVIEW ------------------------------+
| Bandwidth    ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂▁▁▁▁▁▁▁▁▁▂▂▃▁▃▃▃▂▁▁▂▁▁▂▁▁▁▁▁▂▃▂▁▂▂▂▂ 850 Mb/s |
| Hit Rate          ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂▂▃▄▅▅▆▇███▇▇▆▅▄▃▂▂▁▁▂▂▃▄▅▆▆▇███▇▇▆▅▄ 85% |
| Requests        ▂▂▄▄▄▃▂▇▂▇▆▂▂▂▃▄▆▇▂▇▃▇▆▄▆▇▄▁▃▆▅▄▃▃▅▂▂▅▂▅▅▇▄▂▆▇▃▅▂▇▄██▅ 15K/s |
| Connections        ▁▁▁▁▁▁▂▂▂▂▂▂▂▃▃▃▃▃▃▃▄▄▄▄▄▄▅▅▅▅▅▅▅▆▆▆▆▆▆▆▇▇▇▇▇▇▇██████ 800 |
+------------------------------------------------------------------------------+
 12:30:45  proxy.example.com            [G/6] Graphs                 q h 1-6
```

### 120x40 Graph Page (three 40-char boxes)

```
+-------------- NETWORK ---------------++--------------- CACHE ----------------++-------------- DISK I/O --------------+
| Net In       ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂ 850M || Hit Rate      ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂ 85% || Reads         ▂▂▄▄▄▃▂▇▂▇▆▂▂▂▃▄▆▇ 25K |
| Net Out      ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂ 620M || Miss Rate     ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂ 15% || Writes          ▁▁▂▂▃▃▄▄▅▅▅▆▆▇▇██ 8K |
+--------------------------------------++--------------------------------------++--------------------------------------+
+-------------- REQUESTS --------------++------------- RESPONSES --------------++------------ CONNECTIONS -------------+
| Client       ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂  15K || 2xx          ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂ 83% || Client          ▁▁▂▂▃▃▄▄▅▅▅▆▆▇▇██ 800 |
| Origin       ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂  12K || 3xx          ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂ 14% || Origin         ▂▂▄▄▄▃▂▇▂▇▆▂▂▂▃▄▆▇ 400 |
+--------------------------------------++--------------------------------------++--------------------------------------+
+-------------------------------------------- BANDWIDTH HISTORY (last 60s) --------------------------------------------+
| In:            ▂▁▁▂▁▁▁▂▃▄▃▂▃▃▂▂▃▂▁▁▁▁▁▁▁▁▁▂▂▃▁▃▃▃▂▁▁▂▁▁▂▁▁▁▁▁▂▃▂▁▂▂▂▂▂▂▃▂▂▂▁▁▂▂▂▂▃▃▃▃▄▅▅▄▄▅▅▆▆▆▆▆▇███████████▇█ 850M |
| Out:           ▅▅▆▇▇██▇▇▆▅▄▃▂▂▁▁▂▂▃▄▅▅▆▇███▇▇▆▅▄▃▂▂▁▁▂▂▃▄▅▆▆▇███▇▇▆▅▄▃▂▂▁▁▂▂▃▄▅▆▆▇███▇▇▆▅▄▃▂▂▁▁▂▂▃▄▅▆▆▇███▇▆▆▅▄ 620M |
+----------------------------------------------------------------------------------------------------------------------+
 12:30:45  proxy.example.com                           [G/3] Graphs                                            q h 1-3
```

### Block Character Reference

| Height | Char | Description |
|--------|------|-------------|
| 0%     | ` `  | Empty       |
| 12.5%  | `▁`  | Lower 1/8   |
| 25%    | `▂`  | Lower 2/8   |
| 37.5%  | `▃`  | Lower 3/8   |
| 50%    | `▄`  | Lower 4/8   |
| 62.5%  | `▅`  | Lower 5/8   |
| 75%    | `▆`  | Lower 6/8   |
| 87.5%  | `▇`  | Lower 7/8   |
| 100%   | `█`  | Full block  |

### Graph Color Gradient

In interactive mode, graph bars are colored by value:
- **Blue** (0-20%): Low values
- **Cyan** (20-40%): Below average
- **Green** (40-60%): Normal
- **Yellow** (60-80%): Above average
- **Red** (80-100%): High values

Visual: `▁▂▃▄▅▆▇█` with gradient from blue to red
