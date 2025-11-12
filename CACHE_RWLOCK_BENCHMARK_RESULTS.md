# Cache Reader/Writer Lock Implementation - Benchmark Results

## Implementation Summary

### Changes Made

1. **Added `ts::shared_mutex` to `StripeSM`** (`src/iocore/cache/StripeSM.h`)
   - New member: `ts::shared_mutex dir_mutex`
   - Used for directory operations to reduce lock contention

2. **Created RAII Lock Wrappers** (`src/iocore/cache/P_CacheInternal.h`)
   - `CacheDirSharedLock` - for read operations (directory probes)
   - `CacheDirExclusiveLock` - for write operations (directory insert/remove)
   - Macros: `CACHE_DIR_TRY_LOCK_SHARED` and `CACHE_DIR_TRY_LOCK_EXCLUSIVE`

3. **Converted Critical Read Paths** (`src/iocore/cache/Cache.cc`)
   - `Cache::open_read()` - two variants (lines 344-379, 549-588)
   - `Cache::open_write()` - initial probe for updates (lines 684-723)
   - All `directory.probe()` calls now use shared locks
   - Multiple concurrent readers can access directory simultaneously

## Benchmark Results

### Test Configuration
- **Workload**: 1,000,000 requests with 1,000 concurrent clients
- **Origin**: Non-cacheable responses (100% cache misses)
- **Protocol**: HTTP/1.1
- **ATS Config**: Cache enabled, default settings

### Performance Comparison

| Metric | Baseline (Exclusive Locks) | With Reader/Writer Locks | Improvement |
|--------|---------------------------|--------------------------|-------------|
| **Throughput** | 17,520 req/s | 44,218 req/s | **+152% (2.5x)** |
| **Mean Latency** | 55.94ms | 22.23ms | **-60% (2.5x faster)** |
| **Cache Overhead** | 42.81ms | 9.10ms | **-79%** |

### Detailed Metrics

#### Baseline (Before R/W Locks)
```
finished in 57.06s, 17,520.23 req/s, 7.62MB/s
time for request:
  min: 1.78ms   max: 278.30ms   mean: 55.94ms   sd: 10.94ms
```

#### With Reader/Writer Locks (After)
```
finished in 22.62s, 44,218.34 req/s, 19.23MB/s
time for request:
  min: 604us    max: 136.93ms   mean: 22.23ms   sd: 4.33ms
```

#### Cache Disabled (Reference - Upper Bound)
```
finished in 13.31s, 75,130.03 req/s, 32.68MB/s
time for request:
  min: 437us    max: 149.99ms   mean: 13.13ms   sd: 9.27ms
```

## Analysis

### Why the Improvement?

**Before (Exclusive Locks)**:
- Every cache lookup acquired exclusive stripe mutex
- 1000 clients → 1000 serialized lock acquisitions
- Each failed lock = 2ms retry delay
- Average ~21 retries per request = 42ms overhead

**After (Reader/Writer Locks)**:
- Cache lookups (`directory.probe()`) use **shared locks**
- Multiple readers can hold lock simultaneously
- Lock contention drastically reduced
- Retry delays minimized

### Remaining Gap to Cache-Disabled Performance

Current: 44,218 req/s → Target: 75,130 req/s (40% gap remaining)

**Reasons**:
1. **Partial implementation**: Only converted `Cache.cc` read paths
   - `CacheRead.cc` still uses exclusive locks in many places
   - `CacheWrite.cc` write paths not yet converted to exclusive R/W locks

2. **Still using ProxyMutex**: Retained `stripe->mutex` for continuation handling
   - Both locks must be acquired (stripe mutex + dir_mutex)
   - Could be optimized further

3. **Lock acquisition overhead**: Even shared locks have some cost
   - `try_lock_shared()` is not free
   - Multiple lock checks in critical path

### Next Steps for Further Optimization

1. **Complete R/W Lock Conversion**:
   - Convert `CacheRead.cc` probe operations to shared locks
   - Convert `CacheWrite.cc` insert/remove to exclusive R/W locks
   - Estimated gain: +10-20% additional throughput

2. **Optimize Lock Strategy**:
   - Consider removing dual-lock requirement (ProxyMutex + dir_mutex)
   - Integrate R/W lock with continuation scheduling
   - Estimated gain: +5-10% throughput

3. **Consider BRAVO Locks** (`ts::bravo::shared_mutex`):
   - Lock-free fast path for readers under low contention
   - Better performance for read-heavy workloads
   - Estimated gain: +10-15% throughput

4. **Profile Remaining Bottlenecks**:
   - Identify other serialization points
   - Check for unnecessary synchronization

## Conclusion

**Reader/writer locks successfully reduced cache lock contention by 2.5x**, bringing performance from **17,520 req/s to 44,218 req/s** for non-cacheable content.

This is a **partial implementation** covering only the most critical read paths. With full implementation and further optimization, we expect to approach or match the cache-disabled performance of **75,130 req/s**.

### Production Readiness

**Status**: Prototype - needs additional work

**Required before production**:
1. Complete conversion of all cache read/write paths
2. Extensive testing under varied workloads
3. Verify correctness with write-heavy scenarios
4. Performance testing with cacheable content
5. Load testing with mixed read/write workloads

**Risk Assessment**: Low-Medium
- R/W locks are a well-understood pattern
- `ts::shared_mutex` is already used elsewhere in ATS
- Main risk is incomplete conversion leading to edge cases

## Files Modified

1. `src/iocore/cache/StripeSM.h` - Added `dir_mutex` member
2. `src/iocore/cache/P_CacheInternal.h` - Added lock wrapper classes and macros
3. `src/iocore/cache/Cache.cc` - Converted 3 critical read paths to use shared locks

**Total lines changed**: ~100 lines

