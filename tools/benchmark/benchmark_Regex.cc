/** @file

  Benchmarks for the tsutil Regex wrapper: time per operation and the number and size
  of heap allocations each operation makes.

  The allocation half matters as much as the timing half. PCRE2 routes every allocation
  it makes for a compile or a match through the callbacks the wrapper installs, and those
  call the system allocator, so counting calls to malloc across a region counts exactly
  what the wrapper caused. Under the just-in-time engine a match should reach the system
  allocator zero times, because the match data comes out of the caller's own buffer. The
  interpreter is the exception: it allocates a backtracking frames vector through the same
  allocator, so a match that runs interpreted does show up in the count.

  Interposing malloc is only wired up on Linux, where defining these symbols in the
  executable is enough. Elsewhere the counters stay at zero and the report says so, so a
  run on another platform still gives timings without quietly reporting zero allocations
  as a result.

  @section license License

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
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "tsutil/Regex.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

// ---------------------------------------------------------------------------
// Allocation counting
// ---------------------------------------------------------------------------

namespace
{
struct AllocStats {
  unsigned long calls = 0;
  unsigned long bytes = 0;
};

// Counting is per thread so a benchmark that spawns threads does not race the counters.
// These benchmarks are single threaded; the qualifier is here so the numbers stay honest
// if one is added later.
thread_local AllocStats alloc_stats;
thread_local bool       alloc_counting = false;

class CountAllocations
{
public:
  CountAllocations()
  {
    alloc_stats    = AllocStats{};
    alloc_counting = true;
  }
  ~CountAllocations() { alloc_counting = false; }

  AllocStats
  stats() const
  {
    return alloc_stats;
  }
};

#if defined(__linux__)
constexpr bool ALLOC_COUNTING_AVAILABLE = true;
#else
constexpr bool ALLOC_COUNTING_AVAILABLE = false;
#endif

} // namespace

#if defined(__linux__)
#include <dlfcn.h>

// Interpose the system allocator. Defining these in the executable takes precedence over
// libc for every caller in the process, which is what makes the count cover PCRE2's own
// allocations as well as the wrapper's.
namespace
{
using malloc_fn  = void *(*)(size_t);
using free_fn    = void (*)(void *);
using calloc_fn  = void *(*)(size_t, size_t);
using realloc_fn = void *(*)(void *, size_t);

malloc_fn  real_malloc  = nullptr;
free_fn    real_free    = nullptr;
calloc_fn  real_calloc  = nullptr;
realloc_fn real_realloc = nullptr;

// dlsym() itself can allocate while the real pointers are still being resolved. Hand
// those few allocations out of a static buffer rather than recursing.
//
// Each block is preceded by a header holding its size, so a realloc of one can copy the
// old contents rather than silently returning uninitialised storage. The header is one
// max_align_t wide so the pointer handed back keeps the alignment malloc promises.
constexpr size_t BOOTSTRAP_HEADER = alignof(std::max_align_t);
static_assert(BOOTSTRAP_HEADER >= sizeof(size_t), "the bootstrap header must hold a size");

alignas(std::max_align_t) char bootstrap_buffer[16384];
size_t bootstrap_used = 0;
bool   resolving      = false;

// Compare addresses rather than pointers. Relational comparison of pointers into unrelated
// objects has no defined ordering in C++, and a wrong answer here is not harmless: a false
// positive makes free() leak the block and makes realloc() read a header that is not there.
bool
from_bootstrap(void *p)
{
  auto const address = reinterpret_cast<uintptr_t>(p);
  auto const begin   = reinterpret_cast<uintptr_t>(bootstrap_buffer);
  return address >= begin && address < begin + sizeof(bootstrap_buffer);
}

void *
bootstrap_alloc(size_t size)
{
  // Check the request against what is left before rounding it up. Rounding first would let
  // a huge size wrap to a small payload, pass the capacity test, and hand back storage far
  // smaller than asked for. These wrappers stand in for malloc for every library in the
  // process while dlsym resolves, so that block would corrupt somebody else's startup.
  size_t const remaining = sizeof(bootstrap_buffer) - bootstrap_used;
  if (remaining <= BOOTSTRAP_HEADER || size > remaining - BOOTSTRAP_HEADER) {
    return nullptr;
  }

  size_t const payload = (size + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
  if (payload > remaining - BOOTSTRAP_HEADER) {
    return nullptr;
  }
  char *block = bootstrap_buffer + bootstrap_used;
  memcpy(block, &size, sizeof(size));
  bootstrap_used += BOOTSTRAP_HEADER + payload;
  return block + BOOTSTRAP_HEADER;
}

size_t
bootstrap_size(void *p)
{
  size_t size = 0;
  memcpy(&size, static_cast<char *>(p) - BOOTSTRAP_HEADER, sizeof(size));
  return size;
}

// Resolve all four into locals and publish them together, with real_malloc last. dlsym()
// may allocate or free while these lookups are in progress, which re-enters the wrappers
// below; they test their own pointer and fall back to the bootstrap path while it is still
// null, so no wrapper can reach a half-resolved table.
void
resolve_real_allocators()
{
  if (real_malloc != nullptr || resolving) {
    return;
  }
  resolving = true;

  auto *m = reinterpret_cast<malloc_fn>(dlsym(RTLD_NEXT, "malloc"));
  auto *f = reinterpret_cast<free_fn>(dlsym(RTLD_NEXT, "free"));
  auto *c = reinterpret_cast<calloc_fn>(dlsym(RTLD_NEXT, "calloc"));
  auto *r = reinterpret_cast<realloc_fn>(dlsym(RTLD_NEXT, "realloc"));

  real_free    = f;
  real_calloc  = c;
  real_realloc = r;
  real_malloc  = m; // published last: this is the pointer the early return above tests

  resolving = false;
}

void
record(size_t size)
{
  if (alloc_counting) {
    ++alloc_stats.calls;
    alloc_stats.bytes += size;
  }
}
} // namespace

extern "C" void *
malloc(size_t size) noexcept
{
  if (real_malloc == nullptr) {
    resolve_real_allocators();
    if (real_malloc == nullptr) {
      return bootstrap_alloc(size);
    }
  }
  record(size);
  return real_malloc(size);
}

extern "C" void
free(void *p) noexcept
{
  if (p == nullptr || from_bootstrap(p)) {
    return;
  }
  if (real_free == nullptr) {
    resolve_real_allocators();
    if (real_free == nullptr) {
      // Still resolving, so there is nothing to free through. Leaking the few blocks the
      // loader turns over during startup is better than calling through a null pointer.
      return;
    }
  }
  real_free(p);
}

extern "C" void *
calloc(size_t n, size_t size) noexcept
{
  if (real_calloc == nullptr) {
    resolve_real_allocators();
    if (real_calloc == nullptr) {
      // n * size can wrap, which would ask the bootstrap buffer for a small block and then
      // memset a huge one. Refuse rather than compute it.
      if (size != 0 && n > SIZE_MAX / size) {
        return nullptr;
      }
      size_t const total = n * size;
      void        *p     = bootstrap_alloc(total);
      if (p != nullptr) {
        memset(p, 0, total);
      }
      return p;
    }
  }
  record(n * size);
  return real_calloc(n, size);
}

extern "C" void *
realloc(void *p, size_t size) noexcept
{
  // realloc(nullptr, size) is defined to behave as malloc(size), and a caller is entitled
  // to use it that way. Route it through the wrapper above rather than letting it reach the
  // unresolved-pointer path below, which would turn a legitimate allocation into a failure
  // while the resolver is still running.
  if (p == nullptr) {
    return malloc(size);
  }

  if (real_realloc == nullptr) {
    resolve_real_allocators();
  }

  // A block handed out by bootstrap_alloc() is not one the system allocator knows, so it
  // cannot be passed to the real realloc. Move it instead, carrying the old contents over:
  // the loader reallocs while resolving symbols, and handing back uninitialised storage
  // there makes the lookup fail in a way that is very hard to read.
  if (from_bootstrap(p)) {
    size_t const old  = bootstrap_size(p);
    size_t const copy = size < old ? size : old;

    if (real_malloc == nullptr) {
      void *moved = bootstrap_alloc(size);
      if (moved != nullptr) {
        memcpy(moved, p, copy);
      }
      return moved;
    }

    record(size);
    void *moved = real_malloc(size);
    if (moved != nullptr) {
      memcpy(moved, p, copy);
    }
    return moved;
  }

  if (real_realloc == nullptr) {
    // Still resolving, and this block came from neither the bootstrap buffer nor a real
    // allocator this wrapper can reach. Returning a fresh block would be worse than
    // failing: the caller would take uninitialised storage for its moved data while the
    // original went unfreed. Reporting failure is a documented realloc outcome and leaves
    // the caller's pointer valid.
    return nullptr;
  }

  record(size);
  return real_realloc(p, size);
}
#endif // __linux__

// ---------------------------------------------------------------------------
// Corpus
//
// Patterns and subjects taken from what the tree actually matches: remap rules, a host
// allowlist, an extension test, and the crash-guard rule from #5762.
// ---------------------------------------------------------------------------

namespace
{
char const *const PATTERN_PATH      = R"(^/([^/]+)/([^/]+)/(.*)$)";
char const *const PATTERN_HOST      = R"(^(?:[a-z0-9-]+\.)*example\.com$)";
char const *const PATTERN_EXTENSION = R"(\.(jpg|jpeg|png|gif|css|js)$)";
char const *const PATTERN_QUERY     = R"(^/alpha/bravo/[?]((?!action=(newsfeed|calendar|contacts|notepad)).)*$)";

std::string_view const SUBJECT_PATH      = "/images/2026/summer/header.jpg";
std::string_view const SUBJECT_HOST      = "cdn.edge.example.com";
std::string_view const SUBJECT_EXTENSION = "/images/2026/summer/header.jpg";
// A miss for the host pattern: it is a path, not a host.
std::string_view const SUBJECT_MISS = "/no/match/here/at/all";
// A miss for the path pattern, which needs a leading slash and three slash separated
// components. "/no/match/here/at/all" is not one: it satisfies the pattern with a remainder
// of "here/at/all", so using it here would time a hit under the name of a miss.
std::string_view const SUBJECT_PATH_MISS = "not-a-path-at-all";

// A set of host patterns, the shape a rule list has when a caller scans one in order.
std::vector<std::string>
host_patterns(int count)
{
  std::vector<std::string> patterns;
  patterns.reserve(count);
  for (int i = 0; i < count; ++i) {
    patterns.emplace_back("^(?:[a-z0-9-]+\\.)*host" + std::to_string(i) + "\\.example\\.com$");
  }
  return patterns;
}

// Whether this pattern and subject really do reach the just-in-time engine's stack bound
// here. Asking whether PCRE2 produced machine code is not enough: only libpcre2-8 is
// required, a build can be configured without the JIT or refuse this pattern, and a release
// whose JIT uses the stack differently may simply complete the match. In any of those cases
// the subject runs to completion instead, which is a different and much slower measurement
// wearing the same label. Run it once and look at what actually comes back.
bool
exhausts_jit_stack(char const *pattern, std::string const &subject)
{
  // Ask the cheap question first. Without machine code for this pattern there is no JIT
  // stack to exhaust, and running the long subject to find that out would put the whole
  // interpreter cost on a probe whose answer is already known.
  int         errnum    = 0;
  PCRE2_SIZE  erroffset = 0;
  pcre2_code *code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern), PCRE2_ZERO_TERMINATED, 0, &errnum, &erroffset, nullptr);
  if (code == nullptr) {
    return false;
  }
  pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
  size_t jit_size = 0;
  pcre2_pattern_info(code, PCRE2_INFO_JITSIZE, &jit_size);
  pcre2_code_free(code);
  if (jit_size == 0) {
    return false;
  }

  // There is machine code, so the match is bounded by the JIT stack and cannot run away.
  // Now confirm this subject really does reach that bound on this release.
  Regex re;
  if (!re.compile(pattern)) {
    return false;
  }
  RegexMatches matches;
  return re.exec(subject, matches) == PCRE2_ERROR_JIT_STACKLIMIT;
}

// The subject the two JIT stack cases use, built once so the probe and the measurement
// cannot drift apart.
std::string
jit_stack_subject()
{
  std::string subject{"/alpha/bravo/?"};
  subject.append(64 * 1024, 'x');
  return subject;
}

void
report_allocations(char const *label, AllocStats const &stats, unsigned long operations)
{
  if constexpr (!ALLOC_COUNTING_AVAILABLE) {
    printf("  %-44s allocation counting not available on this platform\n", label);
    return;
  }
  printf("  %-44s %8.2f allocations/op %10.1f bytes/op   (%lu ops)\n", label,
         static_cast<double>(stats.calls) / static_cast<double>(operations),
         static_cast<double>(stats.bytes) / static_cast<double>(operations), operations);
}

} // namespace

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

TEST_CASE("Regex compile", "[bench][regex]")
{
  BENCHMARK("compile path pattern")
  {
    Regex re;
    re.compile(PATTERN_PATH);
    return re.empty();
  };

  BENCHMARK("compile host pattern")
  {
    Regex re;
    re.compile(PATTERN_HOST);
    return re.empty();
  };

  BENCHMARK("compile extension pattern")
  {
    Regex re;
    re.compile(PATTERN_EXTENSION);
    return re.empty();
  };

  Regex source;
  source.compile(PATTERN_PATH);
  BENCHMARK("copy a compiled pattern")
  {
    Regex copy(source);
    return copy.empty();
  };
}

TEST_CASE("Regex match", "[bench][regex]")
{
  Regex path;
  path.compile(PATTERN_PATH);
  Regex host;
  host.compile(PATTERN_HOST);
  Regex extension;
  extension.compile(PATTERN_EXTENSION);

  BENCHMARK("bool exec, hit")
  {
    return path.exec(SUBJECT_PATH);
  };

  BENCHMARK("bool exec, miss")
  {
    return host.exec(SUBJECT_MISS);
  };

  BENCHMARK("exec with captures, hit")
  {
    RegexMatches matches;
    return path.exec(SUBJECT_PATH, matches);
  };

  // Built once, outside the timed body: a function-local static would put its
  // initialisation guard inside every iteration and charge the measurement for it.
  RegexMatches reused;
  BENCHMARK("exec with captures, reused matches object")
  {
    return path.exec(SUBJECT_PATH, reused);
  };

  BENCHMARK("exec with captures, miss")
  {
    RegexMatches matches;
    return path.exec(SUBJECT_PATH_MISS, matches);
  };

  BENCHMARK("bool exec, extension pattern")
  {
    return extension.exec(SUBJECT_EXTENSION);
  };

  BENCHMARK("bool exec, host pattern")
  {
    return host.exec(SUBJECT_HOST);
  };
}

TEST_CASE("Regex match with a caller supplied context", "[bench][regex]")
{
  Regex path;
  path.compile(PATTERN_PATH);
  RegexMatchContext context;

  BENCHMARK("exec through the shared context")
  {
    RegexMatches matches;
    return path.exec(SUBJECT_PATH, matches, 0, nullptr);
  };

  BENCHMARK("exec through a caller supplied context")
  {
    RegexMatches matches;
    return path.exec(SUBJECT_PATH, matches, 0, &context);
  };
}

TEST_CASE("DFA set match", "[bench][regex]")
{
  auto const                patterns = host_patterns(20);
  std::vector<const char *> raw;
  raw.reserve(patterns.size());
  for (auto const &p : patterns) {
    raw.push_back(p.c_str());
  }

  DFA dfa;
  dfa.compile(raw.data(), static_cast<int>(raw.size()), RE_UNANCHORED);

  std::string const first{"cdn.host0.example.com"};
  std::string const last{"cdn.host19.example.com"};
  std::string const none{"cdn.nothing.example.org"};

  BENCHMARK("DFA match, first pattern")
  {
    return dfa.match(first);
  };

  BENCHMARK("DFA match, last of 20")
  {
    return dfa.match(last);
  };

  BENCHMARK("DFA match, no match over 20")
  {
    return dfa.match(none);
  };
}

TEST_CASE("Regex match that exhausts the JIT stack", "[bench][regex]")
{
  // A subject long enough to exhaust the JIT stack for this pattern, so the operation
  // measured is the error return rather than a match. This is the shape the crash guard
  // from #5762 covers, and it is the one place a match is expected to cost real time.
  //
  // Note this is not the interpreter: compile() gives the pattern to the JIT, and what the
  // long subject reaches is the JIT's own stack bound. Driving a match onto the interpreter
  // needs a pattern the JIT refuses outright, which starts around 36KB of pattern text and
  // costs tens of seconds per match, so it has no place in a timed suite.
  std::string const subject = jit_stack_subject();
  if (!exhausts_jit_stack(PATTERN_QUERY, subject)) {
    SKIP("this PCRE2 does not reach the JIT stack bound for this pattern and subject");
  }

  Regex query;
  query.compile(PATTERN_QUERY);

  BENCHMARK("exec that exhausts the JIT stack, 64KiB subject")
  {
    RegexMatches matches;
    return query.exec(subject, matches);
  };
}

// ---------------------------------------------------------------------------
// Allocations
//
// Reported rather than asserted: the point of the run is the comparison between two
// implementations, and a hard assertion here would fail on a PCRE2 whose block sizes
// differ from the ones the inline buffer was sized against.
// ---------------------------------------------------------------------------

TEST_CASE("Regex allocation counts", "[bench][regex][alloc]")
{
  constexpr unsigned long OPS = 10000;

  Regex path;
  path.compile(PATTERN_PATH);
  Regex host;
  host.compile(PATTERN_HOST);

  printf("\nAllocations (%s)\n", ALLOC_COUNTING_AVAILABLE ? "counted through an interposed system allocator" : "unavailable");

  {
    // Warm anything that allocates once per thread before counting.
    RegexMatches warm;
    path.exec(SUBJECT_PATH, warm);
  }

  {
    CountAllocations counter;
    for (unsigned long i = 0; i < OPS; ++i) {
      volatile bool r = path.exec(SUBJECT_PATH);
      (void)r;
    }
    report_allocations("bool exec, hit", counter.stats(), OPS);
  }

  {
    CountAllocations counter;
    for (unsigned long i = 0; i < OPS; ++i) {
      volatile bool r = host.exec(SUBJECT_MISS);
      (void)r;
    }
    report_allocations("bool exec, miss", counter.stats(), OPS);
  }

  {
    CountAllocations counter;
    for (unsigned long i = 0; i < OPS; ++i) {
      RegexMatches matches;
      volatile int r = path.exec(SUBJECT_PATH, matches);
      (void)r;
    }
    report_allocations("exec with captures, fresh matches", counter.stats(), OPS);
  }

  {
    RegexMatches     matches;
    CountAllocations counter;
    for (unsigned long i = 0; i < OPS; ++i) {
      volatile int r = path.exec(SUBJECT_PATH, matches);
      (void)r;
    }
    report_allocations("exec with captures, reused matches", counter.stats(), OPS);
  }

  {
    // The set scan, both ends of it. DFA holds a vector of Regex and tries them in order,
    // and each attempt builds a RegexMatches internally, so this is where a per attempt
    // allocation would show up if one existed.
    auto const                patterns = host_patterns(20);
    std::vector<const char *> raw;
    raw.reserve(patterns.size());
    for (auto const &pattern : patterns) {
      raw.push_back(pattern.c_str());
    }
    DFA dfa;
    dfa.compile(raw.data(), static_cast<int>(raw.size()), RE_UNANCHORED);

    std::string const first{"cdn.host0.example.com"};
    std::string const last{"cdn.host19.example.com"};

    {
      CountAllocations counter;
      for (unsigned long i = 0; i < OPS; ++i) {
        volatile int r = dfa.match(first);
        (void)r;
      }
      report_allocations("DFA match, first of 20", counter.stats(), OPS);
    }

    {
      CountAllocations counter;
      for (unsigned long i = 0; i < OPS; ++i) {
        volatile int r = dfa.match(last);
        (void)r;
      }
      report_allocations("DFA match, last of 20", counter.stats(), OPS);
    }
  }

  {
    constexpr unsigned long COMPILES = 1000;
    CountAllocations        counter;
    for (unsigned long i = 0; i < COMPILES; ++i) {
      Regex re;
      re.compile(PATTERN_PATH);
    }
    report_allocations("compile a path pattern", counter.stats(), COMPILES);
  }

  {
    constexpr unsigned long COPIES = 1000;
    CountAllocations        counter;
    for (unsigned long i = 0; i < COPIES; ++i) {
      Regex         copy(path);
      volatile bool r = copy.empty();
      (void)r;
    }
    report_allocations("copy a compiled pattern", counter.stats(), COPIES);
  }

  {
    // The JIT stack limit path: the subject is long enough that the match gives up against
    // the per thread JIT stack rather than completing. It is here to show that giving up
    // costs no allocations either, not to measure the interpreter, which this pattern never
    // reaches on a build that has a JIT. Without a JIT there is no such bound, and the same
    // subject would run to completion on the interpreter and allocate its frames vector,
    // which is a different measurement wearing the same label.
    std::string const subject = jit_stack_subject();
    if (exhausts_jit_stack(PATTERN_QUERY, subject)) {
      Regex query;
      query.compile(PATTERN_QUERY);

      constexpr unsigned long EXECS = 200;
      CountAllocations        counter;
      for (unsigned long i = 0; i < EXECS; ++i) {
        RegexMatches matches;
        volatile int r = query.exec(subject, matches);
        (void)r;
      }
      report_allocations("exec that exhausts the JIT stack", counter.stats(), EXECS);
    } else {
      printf("  %-44s skipped: this PCRE2 does not reach the JIT stack bound here\n", "exec that exhausts the JIT stack");
    }
  }

  printf("\n");
  CHECK(true);
}
