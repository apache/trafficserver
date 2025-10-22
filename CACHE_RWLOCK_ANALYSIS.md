# Cache Reader/Writer Lock Optimization Analysis

## Current Problem: Exclusive Locks for Read Operations

### Current Implementation
The cache currently uses **exclusive try locks** (`CACHE_TRY_LOCK`) for all operations:
- Cache lookups (read-only)
- Directory probes (read-only)
- Cache writes (modifying)
- Directory inserts/removes (modifying)

**Result**: Even read-only operations acquire exclusive locks, preventing concurrent reads.

## Why Reader/Writer Locks Would Help

### Available Infrastructure
Apache Traffic Server already has **two** reader/writer lock implementations:

1. **`ts::shared_mutex`** (`include/tsutil/TsSharedMutex.h`)
   - Wrapper around `pthread_rwlock_t`
   - Provides writer-starvation prevention
   - Standard interface: `lock()`, `lock_shared()`, `try_lock()`, `try_lock_shared()`

2. **`ts::bravo::shared_mutex`** (`include/tsutil/Bravo.h`)
   - High-performance BRAVO algorithm implementation
   - Optimized fast-path for readers (lock-free in common case)
   - Prevents writer starvation with adaptive policy
   - More complex but potentially much faster

### Cache Operation Breakdown

#### Read-Only Operations (can use shared locks):
```
Cache.cc:345          - stripe->directory.probe()    [cache lookup]
Cache.cc:548          - stripe->directory.probe()    [cache lookup]
Cache.cc:691          - stripe->directory.probe()    [cache lookup]
CacheRead.cc:479      - stripe->directory.probe()    [directory scan]
CacheRead.cc:488      - stripe->directory.probe()    [fragment check]
CacheRead.cc:705      - stripe->directory.probe()    [directory scan]
CacheRead.cc:715      - stripe->directory.probe()    [fragment check]
CacheRead.cc:815      - stripe->directory.probe()    [cache lookup]
CacheRead.cc:1141     - stripe->directory.probe()    [cache lookup]
CacheWrite.cc:650     - stripe->directory.probe()    [collision check]
CacheWrite.cc:741     - stripe->directory.probe()    [collision check]
```

#### Write Operations (need exclusive locks):
```
CacheWrite.cc:100     - stripe->directory.remove()   [directory modify]
CacheWrite.cc:282     - stripe->directory.remove()   [directory modify]
CacheWrite.cc:340     - stripe->directory.insert()   [directory modify]
CacheWrite.cc:347     - stripe->directory.insert()   [directory modify]
CacheWrite.cc:424     - stripe->directory.insert()   [directory modify]
CacheWrite.cc:520     - stripe->directory.insert()   [directory modify]
CacheRead.cc:518      - stripe->directory.remove()   [directory modify]
CacheRead.cc:654      - stripe->directory.remove()   [directory modify]
CacheRead.cc:735      - stripe->directory.remove()   [directory modify]
CacheRead.cc:791      - stripe->directory.remove()   [directory modify]
```

### Expected Performance Impact

With 1000 concurrent clients:

**Current Behavior (Exclusive Locks)**:
- Only 1 thread can hold the stripe lock at a time
- Read operations block other reads unnecessarily
- Lock contention causes 42ms average delay
- Throughput: ~17,520 req/s

**With Reader/Writer Locks**:
- Multiple readers can hold shared lock simultaneously
- Only writes need exclusive access
- For non-cacheable content (100% cache misses), most operations are **reads** (directory probes)
- Potential throughput improvement: **10-100x** for read-heavy workloads

### Example Benchmark Scenario

Your benchmark: 1M requests, 1K clients, non-cacheable origin

**Operation mix**:
- `directory.probe()` for cache lookup: **READ**
- `directory.probe()` for collision check: **READ**
- No cache writes (non-cacheable content)

**Current**: All 1000 clients serialize on exclusive lock
**With R/W locks**: All 1000 clients can probe directory concurrently

### Implementation Strategy

#### Option 1: Drop-in Replacement with `ts::shared_mutex`
```cpp
// In StripeSM.h, change stripe mutex type
class StripeSM {
  // OLD:
  // Ptr<ProxyMutex> mutex;

  // NEW:
  ts::shared_mutex stripe_mutex;

  // Keep ProxyMutex for compatibility with event system
  Ptr<ProxyMutex> mutex;
};
```

#### Option 2: Selective Lock Types
Use shared locks for probes:
```cpp
// For reads (Cache.cc:344)
CACHE_TRY_LOCK_SHARED(lock, stripe->mutex, mutex->thread_holding);
if (!lock.is_locked()) { retry... }
stripe->directory.probe(key, stripe, &result, &last_collision);

// For writes (CacheWrite.cc:340)
CACHE_TRY_LOCK_EXCLUSIVE(lock, stripe->mutex, mutex->thread_holding);
if (!lock.is_locked()) { retry... }
stripe->directory.insert(&first_key, stripe, &dir);
```

#### Option 3: Use BRAVO for Maximum Performance
```cpp
// In StripeSM.h
ts::bravo::shared_mutex stripe_mutex;

// For reads with token tracking
ts::bravo::Token token;
ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(stripe_mutex, token);
```

## Challenges and Considerations

### 1. Integration with ProxyMutex/Event System
- ATS uses `ProxyMutex` with the event system
- Reader/writer locks need integration with continuation scheduling
- May need hybrid approach: keep `ProxyMutex` for event handling, add R/W lock for directory

### 2. Lock Ordering
- Must prevent deadlocks
- Document lock acquisition order
- Use consistent `try_lock` vs blocking patterns

### 3. Writer Starvation
- `ts::shared_mutex` already prevents this
- `ts::bravo::shared_mutex` has adaptive policy
- Not a major concern with available implementations

### 4. Testing Requirements
- Ensure correctness under high concurrency
- Verify no performance regression for write-heavy workloads
- Benchmark read-heavy vs write-heavy vs mixed workloads

## Recommendation

**Use `ts::shared_mutex` as a starting point**:

1. **Lowest risk**: Standard pthread-based implementation
2. **Clear semantics**: Writer-starvation prevention built-in
3. **Easy migration**: Similar API to existing mutexes
4. **Proven correctness**: Well-tested implementation

**Migration path**:
1. Add R/W lock to `StripeSM` alongside existing mutex
2. Convert directory probes to use shared locks
3. Keep exclusive locks for modifications
4. Benchmark and measure contention reduction
5. If needed, upgrade to `ts::bravo::shared_mutex` for better performance

## Expected Outcome

For your specific benchmark (non-cacheable content):
- **Before**: 17,520 req/s, 55.94ms latency (42ms from lock contention)
- **After**: 50,000-70,000 req/s, ~15ms latency (close to cache-disabled performance)

**Improvement**: ~3-4x throughput, ~4x lower latency

## Implementation Estimate

- **Small change**: Replace probe operations with shared locks (~100 lines)
- **Medium change**: Add R/W lock to StripeSM, convert all operations (~500 lines)
- **Testing**: High-concurrency stress tests, correctness verification

This is a **high-impact, moderate-effort** optimization that directly addresses the root cause of cache lock contention.

