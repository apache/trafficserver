# Abuse Shield Plugin - Design Document

## Problem Statement

The January 16, 2026 HTTP/2 attack exposed critical gaps:

- `block_errors.so` only tracks 2 of 12 HTTP/2 error codes (missed 99% of attack)
- Attack IPs generated errors with **zero successful requests** - not currently detected
- No unified approach combining error tracking and rate limiting

---

## Features

### Token Bucket Rate Limiting

The plugin uses **Token Bucket algorithm** for accurate per-second rate limiting:

| Feature | Description |
|---------|-------------|
| **Per-IP Request Rate** | `max_req_rate` - Requests per second per IP |
| **Per-IP Connection Rate** | `max_conn_rate` - New connections per second per IP |
| **Per-IP H2 Error Rate** | `max_h2_error_rate` - HTTP/2 errors per second per IP |

**How Token Bucket Works:**
- Each IP gets a bucket with `burst_limit` tokens (equal to rate limit)
- Each event consumes 1 token
- Tokens replenish at the configured rate (e.g., 50 tokens/sec for `max_req_rate: 50`)
- When tokens go negative, the rate limit is exceeded and the IP is blocked
- First request initializes bucket to full (no blocking on first request)

### Three Separate Trackers

The plugin maintains three independent UdiTable trackers:

| Tracker | Hook | Tracks |
|---------|------|--------|
| **TxnTable** | `TXN_START` | Request rate per IP |
| **ConnTable** | `VCONN_START` | Connection rate per IP (SSL only) |
| **H2Table** | `TXN_CLOSE` | HTTP/2 error rate per IP |

### Early Blocking Hooks

The plugin blocks known abusive IPs at the earliest possible point:

| Hook | Protocol | Blocking Point |
|------|----------|----------------|
| `VCONN_START` | HTTPS/H2 | Before TLS handshake (SSL connections only) |
| `SSN_START` | HTTP & HTTPS | At HTTP session start (all protocols) |

This dual-hook approach ensures:
- **SSL connections** are blocked at `VCONN_START` before any TLS negotiation
- **Plain HTTP connections** are blocked at `SSN_START` before any requests are processed
- **All connections** get a second block check at `SSN_START` for defense in depth

### Actions (Independent, Combinable)

Each rule specifies a list of actions. All actions are independent:

| Action | Description |
|--------|-------------|
| `log` | Log the event with token counts to diags.log |
| `block` | Add IP to block list for configured duration |
| `close` | Close the current connection immediately |

**Example combinations:**

- `action: [log]` - Monitor only
- `action: [block]` - Block without logging
- `action: [log, block]` - Log and block

### Log Format

When `log` action is triggered:

```
[abuse_shield] Rule "<name>" matched for IP=<addr>: actions=[log,block] req_tokens=<n> conn_tokens=<n> h2_tokens=<n>
[abuse_shield] Blocking IP <addr> for <n> seconds (rule: <name>)
```

### Operations

| Feature | Description |
|---------|-------------|
| **Dynamic Config Reload** | `traffic_ctl plugin msg abuse_shield.reload` |
| **Data Dump** | `traffic_ctl plugin msg abuse_shield.dump` |
| **Trusted IP Bypass** | Never block IPs in trusted list |

---

## HTTP/2 Error Codes (RFC 9113)

| Code | Name | Type | CVE | Description |
|------|------|------|-----|-------------|
| 0x00 | NO_ERROR | - | - | Graceful shutdown |
| 0x01 | PROTOCOL_ERROR | **Client** | CVE-2019-9513, CVE-2019-9518 | Protocol violation |
| 0x02 | INTERNAL_ERROR | **Server** | - | Internal error |
| 0x03 | FLOW_CONTROL_ERROR | **Client** | CVE-2019-9511, CVE-2019-9517 | Flow control violation |
| 0x04 | SETTINGS_TIMEOUT | **Client** | CVE-2019-9515 | Settings ACK timeout |
| 0x05 | STREAM_CLOSED | **Client** | - | Frame on closed stream |
| 0x06 | FRAME_SIZE_ERROR | **Client** | - | Invalid frame size |
| 0x07 | REFUSED_STREAM | **Server** | - | Stream refused (at capacity) |
| 0x08 | CANCEL | **Client** | CVE-2023-44487 | Stream cancelled (Rapid Reset) |
| 0x09 | COMPRESSION_ERROR | **Client** | CVE-2016-1544 | HPACK compression error |
| 0x0a | CONNECT_ERROR | Either | - | CONNECT method failed |
| 0x0b | ENHANCE_YOUR_CALM | **Server** | - | Rate limit (server overwhelmed) |

**Jan 16 attack:** 0x01 (PROTOCOL_ERROR) and 0x09 (COMPRESSION_ERROR)

---

## Core Algorithm: Udi "King of the Hill"

Adapted from [carp/yahoo/YahooHotSpot.cc](dev/yahoo/ATSPlugins/carp/yahoo/YahooHotSpot.cc) (Patent 7533414).

### Data Structures

```
Hash Table:  IP -> Slot Index     (O(1) lookup)
Slot Array:  [0] [1] [2] ... [N-1]  (fixed size, stores IPSlot)
Contest Ptr: index into slot array  (advances after each contest)
```

### Architecture Diagram

```
                                    UdiTable<Key, Data>
    ┌───────────────────────────────────────────────────────────────────────────┐
    │                                                                           │
    │   lookup_ (unordered_map)              slots_ (vector<Slot>)              │
    │   ┌─────────────────────┐              ┌─────────────────────────────┐    │
    │   │  Key    │  Index    │              │ [0] │ [1] │ [2] │ ... │[N-1]│    │
    │   ├─────────┼───────────┤              └──┬────┬─────┬───────────┬───┘    │
    │   │ 1.2.3.4 │     0     │─────────────────┘    │     │           │        │
    │   │ 5.6.7.8 │     2     │──────────────────────┼─────┘           │        │
    │   │ 9.0.1.2 │    N-1    │────────────────────────────────────────┘        │
    │   └─────────┴───────────┘                                                 │
    │                                                  ▲                        │
    │                                     contest_ptr ─┘                        │
    │                                     (rotates through slots)               │
    │                                                                           │
    └───────────────────────────────────────────────────────────────────────────┘

    Each Slot contains:
    ┌──────────────────────────────────────────┐
    │  Slot                                    │
    │  ├── key: Key (e.g., IP address)         │
    │  ├── score: uint32_t (contest weight)    │
    │  └── data: shared_ptr<Data> ─────────────┼───► IPData (atomic counters)
    └──────────────────────────────────────────┘
```

### Contest Flow Diagram

```
    Event arrives for IP X
            │
            ▼
    ┌───────────────────┐
    │ lookup_.find(X)?  │
    └─────────┬─────────┘
              │
     ┌────────┴────────┐
     │                 │
     ▼                 ▼
  FOUND             NOT FOUND
     │                 │
     ▼                 ▼
┌─────────────┐   ┌─────────────────────────────┐
│ slot.score++│   │ Contest at contest_ptr      │
│ return data │   │                             │
└─────────────┘   │  slot = slots[contest_ptr]  │
                  │  contest_ptr = (ptr+1) % N  │
                  └──────────────┬──────────────┘
                                 │
                    ┌────────────┴────────────┐
                    │                         │
                    ▼                         ▼
           slot.is_empty() OR         X.score <= slot.score
           X.score > slot.score              │
                    │                        ▼
                    ▼                 ┌──────────────┐
            ┌──────────────┐         │ slot.score-- │
            │  X WINS!     │         │ return null  │
            │              │         │ (X not       │
            │ evict old    │         │  tracked)    │
            │ slot.key = X │         └──────────────┘
            │ slot.score=1 │
            │ slot.data=   │
            │  new Data()  │
            │ return data  │
            └──────────────┘
```

### Request Flow

```
1. Event arrives from IP (error, connection, request)

2. Acquire mutex, then hash lookup: Is IP already tracked?

   YES -> slot = slots[lookup[IP]]
          slot.score += score_delta
          return shared_ptr<Data>     // caller updates Data atomically
          -> DONE

   NO  -> Contest for a slot:

          contest_idx = contest_ptr
          contest_ptr = (contest_ptr + 1) % N   // advance pointer

          if (slots[contest_idx].is_empty() || incoming_score > current_score):
              // NEW IP WINS - takes the slot
              if (!slot.is_empty()):
                  lookup.erase(slot.key)     // evict old key
                  evictions++
              slot.key = new_ip
              slot.score = incoming_score
              slot.data = make_shared<Data>()
              lookup[new_ip] = contest_idx
              return slot.data             // caller updates Data
          else:
              // NEW IP LOSES - existing slot survives but weakened
              slot.score--
              return nullptr               // new IP not tracked

3. Caller receives shared_ptr<Data> and updates atomically:

   if (data) {
       data->client_errors.fetch_add(1);
       data->h2_error_counts[error_code].fetch_add(1);
       // Check thresholds, take action if exceeded
   }
```

**Key implementation details:**

- `is_empty()` checks `!data` (not `score == 0`) - fixes stale key bug
- Returns `shared_ptr<Data>` so reference survives eviction
- Score is managed by UdiTable, not exposed to callers
- Data counters use atomics for lock-free updates after mutex released

### Why This Works

| Property | Benefit |
|----------|---------|
| **Rotating pointer** | Every slot eventually contested, no safe havens |
| **Score contest** | Hot IPs win, cold IPs evicted |
| **Success decrements** | Good behavior redeems an IP |
| **Fixed memory** | N slots = bounded memory, no growth |
| **Self-cleaning** | No cleanup thread needed |

**Why:** Fixed memory, self-cleaning, battle-tested in Yahoo CARP plugin.

---

## Data Structures

The plugin uses **three specialized data types**, one for each tracker:

### TxnData (Request Tracker)

```cpp
struct TxnData {
    std::atomic<int32_t>  tokens{0};        // Token bucket (negative = rate exceeded)
    std::atomic<uint64_t> last_update{0};   // Last token update (steady_clock ms)
    std::atomic<uint64_t> blocked_until{0}; // Block expiration (steady_clock ms)

    // Debug counters (observability only, not used for rule matching)
    std::atomic<uint64_t> slot_created{0};  // When slot was created
    std::atomic<uint32_t> count{0};         // Total requests seen

    int32_t consume(int rate, int burst);   // Consume token, return remaining
    bool is_blocked() const;                // Check if blocked_until > now
    void block_until(uint64_t until_ms);    // Set block expiration
};
```

### ConnData (Connection Tracker)

```cpp
struct ConnData {
    std::atomic<int32_t>  tokens{0};
    std::atomic<uint64_t> last_update{0};
    std::atomic<uint64_t> blocked_until{0};

    std::atomic<uint64_t> slot_created{0};  // DEBUG
    std::atomic<uint32_t> count{0};         // DEBUG: Total connections

    int32_t consume(int rate, int burst);
    bool is_blocked() const;
    void block_until(uint64_t until_ms);
};
```

### H2Data (HTTP/2 Error Tracker)

```cpp
constexpr size_t NUM_H2_ERROR_CODES = 16;

struct H2Data {
    std::atomic<int32_t>  tokens{0};
    std::atomic<uint64_t> last_update{0};
    std::atomic<uint64_t> blocked_until{0};

    std::atomic<uint64_t> slot_created{0};                   // DEBUG
    std::atomic<uint32_t> count{0};                          // DEBUG: Total H2 errors
    std::atomic<uint16_t> error_codes[NUM_H2_ERROR_CODES]{}; // DEBUG: Per-code counts

    int32_t consume(int rate, int burst, uint8_t error_code = 0);
    bool is_blocked() const;
    void block_until(uint64_t until_ms);
};
```

### Token Bucket Algorithm

```cpp
int32_t consume_token(std::atomic<int32_t> &tokens,
                      std::atomic<uint64_t> &last_update,
                      int rate_per_sec, int burst_limit)
{
    uint64_t now  = now_ms();  // steady_clock milliseconds
    uint64_t last = last_update.load(std::memory_order_relaxed);

    int32_t current;
    if (last == 0) {
        // First event - initialize with full burst allowance
        current = burst_limit;
    } else {
        // Replenish based on elapsed time
        uint64_t elapsed_ms = now - last;
        int32_t  replenish  = (elapsed_ms * rate_per_sec) / 1000;
        current = tokens.load(std::memory_order_relaxed) + replenish;
        if (current > burst_limit) {
            current = burst_limit;
        }
    }

    last_update.store(now, std::memory_order_relaxed);
    current -= 1;  // Consume one token
    tokens.store(current, std::memory_order_relaxed);
    return current;  // Negative = rate exceeded
}
```

**Memory:** 10,000 slots × 3 trackers ≈ 2.4 MB

### Thread Safety - Current Implementation

The current UdiTable uses a **single global mutex** for simplicity:

```cpp
template<typename Key, typename Data, typename Hash = std::hash<Key>>
class UdiTable {
    mutable std::mutex mutex_;                        // Global lock
    std::unordered_map<Key, size_t, Hash> lookup_;   // Key → slot index
    std::vector<Slot> slots_;                         // Fixed-size slot array
    size_t contest_ptr_{0};                           // Rotating contest pointer

    struct Slot {
        Key key{};
        uint32_t score{0};
        std::shared_ptr<Data> data;                   // Safe reference

        bool is_empty() const { return !data; }       // Check data, not score
    };

public:
    // All operations acquire the global mutex
    std::shared_ptr<Data> find(Key const &key);
    std::shared_ptr<Data> process_event(Key const &key, uint32_t score_delta = 1);
};
```

**Key design decisions:**

| Aspect | Design | Rationale |
|--------|--------|-----------|
| **Locking** | Single `std::mutex` | Simple, correct, sufficient for most workloads |
| **Data ownership** | `shared_ptr<Data>` | Callers hold safe references even after eviction |
| **Empty check** | `!data` (not `score==0`) | Fixed bug where stale keys polluted lookup map |
| **Score** | Plain `uint32_t` | Protected by mutex, no atomic needed |

**Thread safety guarantees:**

| Operation | Locking | Contention |
|-----------|---------|------------|
| `find()` | Mutex lock | Serialized |
| `process_event()` | Mutex lock | Serialized |
| `contest()` | Called with mutex held | Serialized |
| IPData counter updates | Lock-free atomics | Cache-line only |

**Returned `shared_ptr<Data>` is safe:**
- Callers can use the Data even after releasing the table lock
- If the slot is evicted, the shared_ptr keeps the Data alive
- No use-after-free possible

### Benchmark Results: Locking Strategy Comparison

Benchmarked on zeus (16-core x86_64, GCC 15.2.1, Release -O3):

| Strategy | 16 Threads (Zipf) | Notes |
|----------|-------------------|-------|
| **A: Partitioned (16)** | **15.7M ops/sec** | 7x faster, recommended for high-throughput |
| D: Single mutex (current) | 2.2M ops/sec | Simple, sufficient for moderate load |
| C: shared_mutex | 2.0M ops/sec | Upgrade lock overhead hurts |
| B: Hybrid lock | 1.8M ops/sec | Two-phase locking adds overhead |

**Future optimization:** For very high throughput (>5M ops/sec), consider partitioned locking:

```cpp
// Partitioned design (not yet implemented)
template<typename Key, typename Data, size_t NumPartitions = 16>
class UdiTable_Partitioned {
    struct Partition {
        std::mutex mutex;
        std::unordered_map<Key, size_t> lookup;
        std::vector<Slot> slots;
        size_t contest_ptr{0};
    };
    std::array<Partition, NumPartitions> partitions_;

    // Key hash determines partition - threads hitting different partitions run in parallel
    size_t partition_for(Key const &key) { return hasher_(key) % NumPartitions; }
};
```

**Trade-off:** Partitioned design sacrifices global view (keys only compete within their partition) for 7x better throughput at high thread counts

---

## Configuration (YAML)

```yaml
global:
  ip_tracking:
    slots: 10000              # Slots per tracker (3 trackers total)

  blocking:
    duration_seconds: 300     # Block duration (5 minutes)

  trusted_ips_file: /etc/trafficserver/abuse_shield_trusted.txt

# Rules evaluated in order, ALL enabled filters must match (AND logic)
rules:
  # Request rate limiting - 50 requests/second
  - name: "request_rate_limit"
    filter:
      max_req_rate: 50        # Tokens/second (burst = same as rate)
    action: [log, block]

  # Connection rate limiting - 10 connections/second
  - name: "connection_rate_limit"
    filter:
      max_conn_rate: 10
    action: [log, block]

  # HTTP/2 error rate limiting - 5 errors/second
  - name: "h2_error_rate_limit"
    filter:
      max_h2_error_rate: 5
    action: [log, block]

enabled: true
```

### Filter Fields (Token Bucket)

All filters use **token bucket** rate limiting. A rule matches when tokens go negative.

| Field | Description |
|-------|-------------|
| `max_req_rate` | Max requests per second (0 = disabled) |
| `max_conn_rate` | Max connections per second (0 = disabled) |
| `max_h2_error_rate` | Max HTTP/2 errors per second (0 = disabled) |

**Rule Matching Logic:**
- Each filter independently checks its token bucket
- If a filter is configured (non-zero) and tokens are negative, that filter matches
- All enabled filters must match for the rule to trigger (AND logic)
- Burst limit equals the rate (e.g., `max_req_rate: 50` allows burst of 50)

---

## File Structure

### Udi Algorithm (Reusable Library in tsutil)

```
include/tsutil/
└── UdiTable.h                    # Udi "King of the Hill" template class (header-only)

src/tsutil/
├── benchmark_UdiTable.cc         # Throughput benchmark with Zipf distribution
├── benchmark_UdiTable_locking.cc # Locking strategy comparison benchmark
└── unit_tests/
    └── test_UdiTable.cc          # Unit tests (contest, eviction, thread safety)
```

**Thread Safety:** Uses single `std::mutex` for all operations:
- Simple and correct
- All operations serialized
- Returns `shared_ptr<Data>` for safe access after mutex released
- Data counters use atomics for lock-free updates

### Plugin Files

```
plugins/experimental/abuse_shield/
├── CMakeLists.txt            # Build (links yaml-cpp, tsutil)
├── README.md                 # Usage docs
├── abuse_shield.cc           # Plugin entry, hooks, YAML parsing
├── abuse_shield.yaml         # Sample config
├── abuse_shield_trusted.txt  # Sample trusted IPs file
├── ip_data.h                 # IPData struct and IPTable type alias
└── ip_data.cc                # IPData implementation
```

### Tests

```
tests/gold_tests/pluginTest/abuse_shield/
├── abuse_shield.test.py      # Autest - end-to-end plugin tests
├── abuse_shield.yaml         # Test config
└── gold/                     # Expected output files
```

### Documentation (Sphinx)

```
doc/admin-guide/plugins/
└── abuse_shield.en.rst       # Admin guide documentation
```

### Trusted IPs File Format

```
# abuse_shield_trusted.txt
# One IP or CIDR per line, # for comments

# Localhost
127.0.0.1
::1

# Internal networks
10.0.0.0/8
172.16.0.0/12
192.168.0.0/16

# Monitoring servers
# 203.0.113.50

# Load balancers
# 198.51.100.0/24
```

---

## Metrics

All metrics are prefixed with `abuse_shield.`

### Current Metrics

These ATS stats are exposed via `traffic_ctl metric get abuse_shield.*`:

| Metric | Type | Description |
|--------|------|-------------|
| `rules.matched` | counter | Total times any rule matched (tokens negative) |
| `actions.blocked` | counter | Total times block action executed |
| `actions.closed` | counter | Total times close action executed |
| `actions.logged` | counter | Total times log action executed |
| `connections.rejected` | counter | SSL connections rejected (blocked IP at VCONN_START) |
| `sessions.rejected` | counter | HTTP sessions rejected (blocked IP at SSN_START) |
| `tracker.events` | counter | Total events processed |
| `tracker.slots_used` | gauge | Currently occupied slots (sum of all 3 trackers) |
| `tracker.contests` | counter | Total contest attempts |
| `tracker.contests_won` | counter | Contests where new IP took the slot |
| `tracker.evictions` | counter | IPs evicted due to lost contests |

**Example:**
```bash
traffic_ctl metric get abuse_shield.rules.matched
traffic_ctl metric get abuse_shield.actions.blocked
traffic_ctl metric get abuse_shield.connections.rejected
```

### Dump Output

The `abuse_shield.dump` command outputs tracker state:

```
# abuse_shield dump (token bucket rate limiting)
# Negative tokens indicate rate exceeded

# Transaction (Request) tracker
# slots_used: 3 / 10000
# contests: 3 (won: 3)
# evictions: 0
10.0.1.2    tokens=-448    count=502    blocked=91011139
127.0.0.1   tokens=49      count=1      blocked=0

# Connection tracker
# slots_used: 2 / 10000
# contests: 2 (won: 2)
# evictions: 0
10.0.1.2    tokens=9       count=13     blocked=91011139

# H2 Error tracker
# slots_used: 0 / 10000
# contests: 0 (won: 0)
# evictions: 0
```

**Interpreting the dump:**
- `tokens=-448`: Rate exceeded (negative = blocked)
- `count=502`: Debug counter - total events seen
- `blocked=91011139`: Blocked until timestamp (steady_clock ms)
- `score`: UdiTable contest score (higher = more active)

---

## Implementation Status

### Completed

**Core Library (tsutil):**
- UdiTable template class - reusable thread-safe Udi algorithm
- Unit tests for UdiTable (contest logic, eviction, thread safety)
- Partitioned locking support for high throughput

**Plugin:**
- **Token Bucket algorithm** for all rate limiting
- **Three separate trackers** (TxnTable, ConnTable, H2Table)
- Per-IP request rate limiting (`max_req_rate`)
- Per-IP connection rate limiting (`max_conn_rate`)
- Per-IP H2 error rate limiting (`max_h2_error_rate`)
- Configurable blocking with duration
- Actions: log, block, close
- Trusted IP bypass (from separate file)
- YAML config with dynamic reload
- Data dump via plugin message
- Metrics for monitoring

**Testing:**
- Homelab testing (hera/eris) - see TEST_REPORT.md
- Rate limiting verified (request rate, connection blocking, expiration)

**Documentation:**
- DESIGN.md (this file)
- Sphinx docs (doc/admin-guide/plugins/abuse_shield.en.rst)
- TEST_REPORT.md (test results)

### Known Limitations

1. **Keep-alive connections** - Existing connections continue working after an IP is blocked. Only NEW connections/sessions are rejected.

2. **VCONN_START fires for SSL only** - The earliest blocking hook (`VCONN_START`) only fires for SSL connections. Plain HTTP connections are blocked at `SSN_START` instead (still before any requests are processed).

### Future Enhancements

- rate_limit action (return 429 Too Many Requests instead of closing connection)
- Cluster host sharing (sync blocked IPs across ATS hosts)
- MaxMind GeoIP integration