# SafeT-Span UDI Insights for Rate Limiting

## Overview

Analysis of the safet-span implementation (`~/src/safet-span`) reveals several
sophisticated patterns for table sizing, decay mechanisms, and slot eviction
that could significantly enhance the current UdiTable implementation.

SafeT-span is a mature, production-tested distributed rate limiting system.
Several patterns differ significantly from the current `UdiTable` implementation
and could be valuable additions.

## Key Findings

### 1. Time-Based Decay (EWMA) vs Simple Counter

**SafeT-Span Approach:**

The safet-span uses Exponential Weighted Moving Average for scores rather than simple increment/decrement:

```cpp
// From Calculate.h - CalculateEWMA::update()
double delta_T = config_->current_time_ - slot.timestamp_;
double factor = std::exp(-1 * delta_T * getFloatParam(WINDOW));
base[ESTIMATE].d = factor * base[ESTIMATE].d + config_->input_[0].d;
result[0].d = base[ESTIMATE].d * getFloatParam(WINDOW);
```

**For scoring/eviction decisions, they apply the same decay:**

```cpp
// Calculate.h - CalculateEWMA::score()
double delta_T = config_->current_time_ - slot.timestamp_;
double factor = exp(-delta_T * getFloatParam(WINDOW));
return factor * base[ESTIMATE].d * getFloatParam(WINDOW);
```

**Benefit:** Scores decay naturally over time. An IP that was hot 5 minutes ago but went silent will have a much lower score than one actively generating events now.

**Current UdiTable approach:** Simple `slot.score++` and `slot.score--` in contest. Old scores persist indefinitely unless contested.

### 2. Smarter Eviction with Multiple Probes

**SafeT-Span Approach:**

When table is full, they don't just look at one slot. They probe multiple random candidates using a fast PRNG (xoroshiro128+):

```cpp
// TrackingDataSlotCache.h - get() method
auto* candidate = eviction ? &back_slot : nullptr;
auto candidate_score = score;

for (size_t i = 0; i < eviction_attempts_; ++i) {
    const auto index = callback_.evictionCandidate(size());
    auto& slot = at(index);
    std::tie(eviction, score) = validator(slot.data);
    if (eviction && score < candidate_score) {
        candidate = &slot;
        candidate_score = score;
    }
}
```

**Default:** `REPLACEMENT_PROBES = 4` (see `Tracking.h` line 47)

**Random number generation:** They use xoroshiro128+, a fast non-cryptographic PRNG seeded from `/dev/urandom`:

```cpp
// Rand.h - Each tracking table has its own RNG instance
uint32_t rand_hand(size_t range) {
    if (replacement_probes_ < 1) {
        ++clock_hand_;  // Sequential if probes disabled
    } else {
        clock_hand_ = random_.rand32();  // Random if probes enabled
    }
    return clock_hand_ % range;
}
```

**Benefit:** This is the "power of two choices" principle (or power of four in this case). By picking 4 random slots and evicting the one with the lowest score, you have much better odds of finding a low-value slot than with a single rotating pointer.

**Current UdiTable approach:** Single rotating pointer, whichever slot it points to is the contest target.

### 3. Window-Based Expiration

**SafeT-Span Approach:**

They combine LRU with window-based expiration:

```cpp
// TrackingDataSlotCache.h - get() method
if (back_slot.timestamp + static_cast<int64_t>(window_) <= timestamp && eviction) {
    callback_.onEviction(back_slot.key, back_slot.data);
    erase(back_slot);
}
```

**Benefit:** Stale entries (older than window time) are automatically expired, preventing zombie entries from consuming slots.

**Current UdiTable approach:** No time-based expiration; entries persist until contested away.

### 4. Sharding for Lock Contention

**SafeT-Span Approach:**

Status table uses 16 shards to reduce lock contention:

```cpp
// Table.h
static constexpr size_t TABLE_SHARD_BITS = 4;
static constexpr size_t TABLE_SHARDS = (1 << TABLE_SHARD_BITS);

inline uint32_t shard(const Key& key) const {
    return static_cast<uint32_t>(key[1] & (TABLE_SHARDS - 1));
}
```

**Our benchmarks showed:** Partitioned (16 shards) achieved 15.7M ops/sec vs 2.2M for single mutex (per DESIGN.md).

### 5. Configurable Table Size Per Use Case

**SafeT-Span Approach:**

Table size is runtime configurable per table:

```lua
table = span.init("mail", "ip", 10000, 60, 20, 256)
--                              ^^^^^ table_size
```

With tracked metrics:
- `_table_name_.size` - configured size
- `_table_name_.tracking_size` - current occupied slots
- `_table_name_.tracking_capacity` - allocated capacity

### 6. Cardinality Tracking and Decay (For Future Reference)

SafeT-span supports **cardinality tracking** - counting the number of unique values associated with a key. For example, if tracking by IP address, cardinality could measure how many unique URLs that IP accessed.

This is useful for detecting:
- **URL scanners**: High cardinality of requested paths
- **Credential stuffing**: High cardinality of usernames at login endpoint
- **Distributed attacks**: Single IP hitting many unique targets

They use a probabilistic bitmap (similar to Bloom filter) with decay:

```cpp
// Calculate.h - CalculateCardinality::update()
if (leak > 0.0) {
    while (base[TIMER].d > leak) {
        // Clear a random bit from the bitmap
        uint64_t word = config_->rng_.rand64() >> 4;
        const uint32_t bit = word & 0x3f;
        word = (word >> 6) % nwords;
        annex[word].u &= ~(1ul << bit);
        base[TIMER].d -= leak;
    }
}
```

## Recommended Enhancements

### Option A: Add EWMA Scoring (Medium Effort)

Replace simple score counter with time-decayed EWMA:

```cpp
struct Slot {
    Key key{};
    double ewma_score{0.0};      // EWMA instead of uint32_t
    double last_update_time{0.0}; // Timestamp for decay calculation
    data_ptr data;

    double decayed_score(double now, double window_inverse) const {
        double delta_t = now - last_update_time;
        return ewma_score * std::exp(-delta_t * window_inverse);
    }
};
```

**Files to modify:** `include/tsutil/UdiTable.h`

### Option B: Add Multi-Probe Eviction (Low Effort)

Instead of single contest slot, probe N random slots and evict lowest:

```cpp
data_ptr contest(Key const &key, uint32_t incoming_score) {
    Slot* best_candidate = nullptr;
    double best_score = std::numeric_limits<double>::max();

    for (size_t probe = 0; probe < num_probes_; ++probe) {
        size_t idx = (contest_ptr_ + probe) % slots_.size();
        Slot& slot = slots_[idx];
        double score = slot.decayed_score(now, window_inverse_);
        if (slot.is_empty() || score < best_score) {
            best_candidate = &slot;
            best_score = score;
        }
    }

    if (incoming_score > best_score) {
        // Evict best_candidate and insert new key
    }
}
```

### Option C: Add Window-Based Expiration (Low Effort)

Auto-expire entries older than window time:

```cpp
if (slot.last_update_time + window_seconds_ < now) {
    // Entry is stale, evict without contest
}
```

## Summary Table

| Feature | SafeT-Span | Current UdiTable | Recommendation |
|---------|------------|------------------|----------------|
| Score decay | EWMA (time-based) | None (persist forever) | Add EWMA |
| Eviction strategy | Multi-probe (4 random) + LRU | Single rotating pointer | Add multi-probe |
| Stale entry handling | Window-based expiration | None | Add time-based expiry |
| Sharding | 16 shards | Single mutex | Consider for high throughput |
| Table size | Runtime config | Runtime config | Already good |
| Metrics | Extensive (replaced, rejected, etc.) | Good basics | Sufficient |

## Open Questions

1. Should we add EWMA-based scoring? (provides natural time decay)
2. Should we add multi-probe eviction? (4 random probes instead of 1)
3. Should we add window-based expiration for stale entries?
