/** @file

  A brief file description

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

#include <tsutil/Regex.h>
#include <tsutil/Assert.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <pthread.h>

#include <array>
#include <vector>
#include <mutex>
#include <utility>

static_assert(RE_CASE_INSENSITIVE == PCRE2_CASELESS, "Update RE_CASE_INSENSITIVE for current PCRE2 version.");
static_assert(RE_UNANCHORED == PCRE2_MULTILINE, "Update RE_UNANCHORED for current PCRE2 version.");
static_assert(RE_ANCHORED == PCRE2_ANCHORED, "Update RE_ANCHORED for current PCRE2 version.");
static_assert(RE_NOTEMPTY == PCRE2_NOTEMPTY, "Update RE_NOTEMPTY for current PCRE2 version.");

static_assert(RE_ERROR_NOMATCH == PCRE2_ERROR_NOMATCH, "Update RE_ERROR_NOMATCH for current PCRE2 version.");
static_assert(RE_ERROR_NULL == PCRE2_ERROR_NULL, "Update RE_ERROR_NULL for current PCRE2 version.");

// PCRE2 10.30 added PCRE2_ENDANCHORED. Older PCRE2 (e.g., CentOS 7 ships 10.23) lacks it.
// On modern PCRE2 we pass RE_ENDANCHORED through natively (zero overhead); on old PCRE2 the
// bit is not a valid pcre2_compile option and would be rejected with PCRE2_ERROR_BADOPTION,
// so we transparently rewrite the pattern to "(?:pattern)\z" and strip the bit. See
// Regex::compile() for the rewrite. This preserves alternation-with-backtracking semantics
// (unlike a post-match length check, which stops at the first successful alternative).
#ifdef PCRE2_ENDANCHORED
static constexpr bool ATS_PCRE2_HAS_ENDANCHORED = true;
static_assert(RE_ENDANCHORED == PCRE2_ENDANCHORED, "Update RE_ENDANCHORED for current PCRE2 version.");
static_assert((RE_FULL_MATCH & PCRE2_ENDANCHORED) == 0, "RE_FULL_MATCH bit collides with PCRE2_ENDANCHORED");
#else
static constexpr bool ATS_PCRE2_HAS_ENDANCHORED = false;
#endif

// RE_FULL_MATCH is an ATS-only flag; it must not collide with any PCRE2 compile or match flag.
// We strip it before forwarding to pcre2_match, but a collision would cause spurious behavior
// if someone OR'd it into a flag word that's also passed elsewhere.
static_assert((RE_FULL_MATCH & PCRE2_ANCHORED) == 0, "RE_FULL_MATCH bit collides with PCRE2_ANCHORED");
static_assert((RE_FULL_MATCH & PCRE2_NO_UTF_CHECK) == 0, "RE_FULL_MATCH bit collides with PCRE2_NO_UTF_CHECK");
static_assert((RE_FULL_MATCH & PCRE2_CASELESS) == 0, "RE_FULL_MATCH bit collides with PCRE2_CASELESS");
static_assert((RE_FULL_MATCH & PCRE2_MULTILINE) == 0, "RE_FULL_MATCH bit collides with PCRE2_MULTILINE");
static_assert((RE_FULL_MATCH & PCRE2_NOTEMPTY) == 0, "RE_FULL_MATCH bit collides with PCRE2_NOTEMPTY");

//----------------------------------------------------------------------------
namespace
{
void *
my_malloc(size_t size, void * /*caller*/)
{
  void *ptr = malloc(size);
  return ptr;
}

void
my_free(void *ptr, void * /*caller*/)
{
  free(ptr);
}

//----------------------------------------------------------------------------
// One match context is shared by every thread that matches through it, and PCRE2
// requires a distinct JIT stack per thread, so the stack comes from a callback
// invoked at match time rather than a pointer baked in when the context is built.
//
// The per thread stack is held in a pthread key rather than a thread_local. A
// thread_local with a destructor registers it through __cxa_thread_atexit, which
// takes the dynamic loader lock; doing that from a match would invert lock order
// against a dlopen caller running a plugin's static initialization. See the same
// hazard described at Diags::tag_activated. A pthread key registers its destructor
// once, at key creation, and never from the matching path.
pthread_key_t  jit_stack_key;
bool           jit_stack_key_valid = false;
pthread_once_t jit_stack_key_once  = PTHREAD_ONCE_INIT;

void
destroy_jit_stack(void *stack)
{
  if (stack != nullptr) {
    pcre2_jit_stack_free(static_cast<pcre2_jit_stack *>(stack));
  }
}

void
make_jit_stack_key()
{
  jit_stack_key_valid = pthread_key_create(&jit_stack_key, destroy_jit_stack) == 0;
}

pcre2_jit_stack *
jit_stack_for_this_thread(void *)
{
  pthread_once(&jit_stack_key_once, make_jit_stack_key);
  if (!jit_stack_key_valid) {
    // Without a key there is nowhere to keep a stack, and jit_stack_key holds a
    // default value that may name an unrelated key. Returning null tells PCRE2 to
    // use its own default stack, which pcre2jit documents as thread safe.
    return nullptr;
  }

  auto *stack = static_cast<pcre2_jit_stack *>(pthread_getspecific(jit_stack_key));
  if (stack == nullptr) {
    // One page to start, one mebibyte at most. The maximum is address space reserved at
    // creation and made resident only as deep as a match actually goes, so a larger one
    // costs nothing per match.
    //
    // It does NOT cover every subject a client can send. Measured with the pattern the
    // unit tests use, a mebibyte resolves about 26,213 characters, while
    // proxy.config.http.request_header_max_size defaults to 32768. A longer subject
    // falls back to PCRE2's own stack and can still hit a resource-exhaustion code.
    stack = pcre2_jit_stack_create(4096, 1024 * 1024, nullptr);
    if (pthread_setspecific(jit_stack_key, stack) != 0) {
      // Nothing holds the stack now, so it would leak once per match. Give it back and
      // let PCRE2 use its own default stack for this call.
      pcre2_jit_stack_free(stack);
      return nullptr;
    }
  }
  return stack;
}

//----------------------------------------------------------------------------
class RegexContext
{
public:
  static RegexContext *
  get_instance()
  {
    thread_local RegexContext ctx;
    return &ctx;
  }
  ~RegexContext()
  {
    if (_general_context != nullptr) {
      pcre2_general_context_free(_general_context);
    }
    if (_compile_context != nullptr) {
      pcre2_compile_context_free(_compile_context);
    }
    if (_match_context != nullptr) {
      pcre2_match_context_free(_match_context);
    }
  }
  pcre2_general_context *
  get_general_context()
  {
    return _general_context;
  }
  pcre2_compile_context *
  get_compile_context()
  {
    return _compile_context;
  }
  pcre2_match_context *
  get_match_context()
  {
    return _match_context;
  }

private:
  RegexContext()
  {
    _general_context = pcre2_general_context_create(my_malloc, my_free, nullptr);
    _compile_context = pcre2_compile_context_create(_general_context);
    _match_context   = pcre2_match_context_create(_general_context);
    pcre2_jit_stack_assign(_match_context, jit_stack_for_this_thread, nullptr);
  }
  pcre2_general_context *_general_context = nullptr;
  pcre2_compile_context *_compile_context = nullptr;
  pcre2_match_context   *_match_context   = nullptr;
};

} // namespace

//----------------------------------------------------------------------------
struct RegexMatches::_MatchData {
  static pcre2_match_data *
  get(_MatchDataPtr const &p)
  {
    return static_cast<pcre2_match_data *>(p._ptr);
  }
  static void
  set(_MatchDataPtr &p, pcre2_match_data *ptr)
  {
    p._ptr = ptr;
  }
};

//----------------------------------------------------------------------------
RegexMatches::RegexMatches(uint32_t size)
{
  pcre2_general_context *ctx = pcre2_general_context_create(&RegexMatches::malloc, &RegexMatches::free, static_cast<void *>(this));

  pcre2_match_data *match_data = pcre2_match_data_create(size, ctx);
  debug_assert_message(match_data, "Failed to allocate pcre2 match data from custom context");

  _MatchData::set(_match_data, match_data);
}

//----------------------------------------------------------------------------
void *
RegexMatches::malloc(size_t size, void *caller)
{
  auto *matches = static_cast<RegexMatches *>(caller);

  // allocate from the buffer if possible
  if (size <= sizeof(matches->_buffer) - matches->_buffer_bytes_used) {
    void *ptr                    = matches->_buffer + matches->_buffer_bytes_used;
    matches->_buffer_bytes_used += size;
    return ptr;
  }

  return ::malloc(size);
}

//----------------------------------------------------------------------------
void
RegexMatches::free(void *p, void *caller)
{
  auto *matches = static_cast<RegexMatches *>(caller);

  // Call free for any p outside _buffer
  // If the pcre2 context requests more data than fits in our builtin buffer, we will call malloc
  // to fulfil that request.
  // !his checks for any pointers outside of our buffer in order to free that memory up.
  //
  // nullptr is outside of our buffer, but its ok to call ::free with nullptr.
  if (!(p >= matches->_buffer && p < matches->_buffer + sizeof(matches->_buffer))) {
    ::free(p);
  }
}

//----------------------------------------------------------------------------
RegexMatches::~RegexMatches()
{
  auto ptr = _MatchData::get(_match_data);
  if (ptr != nullptr) {
    pcre2_match_data_free(ptr);
  }
}

//----------------------------------------------------------------------------
size_t *
RegexMatches::get_ovector_pointer()
{
  return pcre2_get_ovector_pointer(_MatchData::get(_match_data));
}

//----------------------------------------------------------------------------
int32_t
RegexMatches::size() const
{
  return _size;
}

//----------------------------------------------------------------------------
std::string_view
RegexMatches::operator[](size_t index) const
{
  // The ovector is allocated with a fixed number of pairs, but pcre2_match() writes only as far as
  // the highest participating group. Every pair past that keeps whatever _buffer happened to hold,
  // so the allocated count is not a usable bound -- _size is what the match actually populated.
  if (_size <= 0 || index >= static_cast<size_t>(_size)) {
    return "";
  }

  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(_MatchData::get(_match_data));

  // Within _size a group can still have not participated, in which case PCRE2 sets both of its
  // offsets to PCRE2_UNSET. An optional group preceding a participating one is the usual way.
  if (PCRE2_UNSET == ovector[2 * index]) {
    return "";
  }

  return std::string_view(_subject.data() + ovector[2 * index], ovector[2 * index + 1] - ovector[2 * index]);
}

//----------------------------------------------------------------------------
struct RegexMatchContext::_MatchContext {
  static pcre2_match_context *
  get(_MatchContextPtr const &p)
  {
    return static_cast<pcre2_match_context *>(p._ptr);
  }
  static void
  set(_MatchContextPtr &p, pcre2_match_context *ptr)
  {
    p._ptr = ptr;
  }
};

//----------------------------------------------------------------------------
RegexMatchContext::RegexMatchContext()
{
  // A blank context silently drops the JIT stack callback, which is how this type
  // came to run with PCRE2's fallback 32KiB stack instead of the 1MiB one. Assign
  // the callback directly rather than copying the shared context.
  //
  // Copying it would mean calling RegexContext::get_instance(), and that constructs
  // a thread_local whose destructor registers through __cxa_thread_atexit, taking
  // the dynamic loader lock. A plugin that builds one of these during its static
  // initialization is already inside dlopen holding that lock, so reaching it from
  // this constructor would invert lock order for exactly the reason described above
  // jit_stack_key. Nothing else the shared context carries is needed here: the
  // callback is thread independent because it resolves its stack through the
  // pthread key, and a null general context is what this constructor used before.
  auto *ctx = pcre2_match_context_create(nullptr);

  debug_assert_message(ctx, "Failed to obtain a pcre2 match context");
  if (ctx != nullptr) {
    pcre2_jit_stack_assign(ctx, jit_stack_for_this_thread, nullptr);
  }
  _MatchContext::set(_match_context, ctx);
}

//----------------------------------------------------------------------------
RegexMatchContext::RegexMatchContext(RegexMatchContext const &other)
{
  auto ptr = _MatchContext::get(other._match_context);
  if (nullptr != ptr) {
    pcre2_match_context *const ctx = pcre2_match_context_copy(ptr);
    _MatchContext::set(_match_context, ctx);
  }
}

//----------------------------------------------------------------------------
RegexMatchContext &
RegexMatchContext::operator=(RegexMatchContext const &other)
{
  if (&other != this) {
    auto ptr = _MatchContext::get(other._match_context);

    // Take the copy before releasing what this object already holds, so a failing
    // copy leaves it holding its old context rather than a freed one. Releasing it
    // is what this operator used to omit, and every assignment leaked one context.
    pcre2_match_context *const ctx = nullptr != ptr ? pcre2_match_context_copy(ptr) : nullptr;

    // pcre2_match_context_copy() returns nullptr when it cannot allocate. Assigning it
    // anyway would free the old context and leave this object empty, which is the very
    // thing the ordering above exists to prevent, so leave the object untouched instead.
    if (nullptr != ptr && nullptr == ctx) {
      return *this;
    }

    pcre2_match_context *const old = _MatchContext::get(_match_context);

    _MatchContext::set(_match_context, ctx);
    if (old != nullptr) {
      pcre2_match_context_free(old);
    }
  }
  return *this;
}

//----------------------------------------------------------------------------
RegexMatchContext::RegexMatchContext(RegexMatchContext &&that) noexcept
{
  // Through the typed accessors rather than std::exchange on the raw member: _ptr is
  // void *, and void * does not implicitly convert to pcre2_match_context *, which is
  // what set() takes.
  _MatchContext::set(_match_context, _MatchContext::get(that._match_context));
  _MatchContext::set(that._match_context, nullptr);
}

//----------------------------------------------------------------------------
RegexMatchContext &
RegexMatchContext::operator=(RegexMatchContext &&that) noexcept
{
  if (this != &that) {
    if (auto *const old = _MatchContext::get(_match_context); old != nullptr) {
      pcre2_match_context_free(old);
    }
    _MatchContext::set(_match_context, _MatchContext::get(that._match_context));
    _MatchContext::set(that._match_context, nullptr);
  }
  return *this;
}

//----------------------------------------------------------------------------
RegexMatchContext::~RegexMatchContext()
{
  // No assert that the pointer is set. Null is now a legitimate state: a moved-from
  // object holds nothing, and asserting here would abort a debug build on the first
  // destruction of one. Before the move operations existed the only way to reach this
  // with null was a failed construction, which is why the assert was reasonable then.
  if (auto *const ptr = _MatchContext::get(_match_context); ptr != nullptr) {
    pcre2_match_context_free(ptr);
  }
}

//----------------------------------------------------------------------------
void
RegexMatchContext::set_match_limit(uint32_t limit)
{
  auto ptr = _MatchContext::get(_match_context);
  debug_assert_message(ptr, "Failed to get the match context");
  if (ptr != nullptr) {
    pcre2_set_match_limit(ptr, limit);
  }
}

//----------------------------------------------------------------------------
struct Regex::_Code {
  static pcre2_code *
  get(_CodePtr const &p)
  {
    return static_cast<pcre2_code *>(p._ptr);
  }
  static void
  set(_CodePtr &p, pcre2_code *ptr)
  {
    p._ptr = ptr;
  }
};

//----------------------------------------------------------------------------
Regex::Regex(Regex const &other)
{
  auto *other_code = _Code::get(other._code);
  if (other_code != nullptr) {
    // Use PCRE2's built-in function to deep copy the compiled pattern
    auto *copied_code = pcre2_code_copy(other_code);

    // Null when pcre2 could not obtain memory. Leave the object empty rather than
    // holding a null pattern.
    if (copied_code != nullptr) {
      // The copy does not carry the machine code the JIT produced, so without this it
      // matches on the interpreter, under different resource limits than the original.
      // Unchecked for the same reason compile() does not check it.
      pcre2_jit_compile(copied_code, PCRE2_JIT_COMPLETE);

      _Code::set(_code, copied_code);
    }
  }
}

//----------------------------------------------------------------------------
Regex &
Regex::operator=(Regex const &other)
{
  if (this != &other) {
    // Use copy-and-swap idiom: create a temporary copy, then swap with it
    Regex temp(other); // Copy constructor does the deep copy

    // Swap the internal pointers
    std::swap(_code, temp._code);
    // temp's destructor will clean up our old _code
  }
  return *this;
}

//----------------------------------------------------------------------------
Regex::Regex(Regex &&that) noexcept
{
  _code = that._code;
  _Code::set(that._code, nullptr);
}

//----------------------------------------------------------------------------
Regex &
Regex::operator=(Regex &&other)
{
  if (this != &other) {
    auto ptr = _Code::get(_code);
    if (ptr != nullptr) {
      pcre2_code_free(ptr);
    }
    _code = other._code;
    _Code::set(other._code, nullptr);
  }
  return *this;
}

//----------------------------------------------------------------------------
Regex::~Regex()
{
  auto ptr = _Code::get(_code);
  if (ptr != nullptr) {
    pcre2_code_free(ptr);
  }
}

//----------------------------------------------------------------------------
bool
Regex::compile(std::string_view pattern, uint32_t flags)
{
  std::string error;
  int         erroroffset;

  return this->compile(pattern, error, erroroffset, flags);
}

//----------------------------------------------------------------------------
bool
Regex::compile(std::string_view pattern, std::string &error, int &erroroffset, uint32_t flags)
{
  // get the RegexContext instance - should only be null when shutting down
  RegexContext *regex_context = RegexContext::get_instance();
  if (regex_context == nullptr) {
    return false;
  }

  // On PCRE2 < 10.30 the ENDANCHORED bit is not a valid pcre2_compile option. Rewrite
  // the pattern to "(?:pattern)\z" and strip the bit so pcre2 enforces end-of-subject
  // natively (including proper alternation backtracking). Zero overhead on modern PCRE2
  // where the bit is passed through unchanged.
  std::string      rewritten_pattern;
  std::string_view effective_pattern = pattern;
  bool             pattern_wrapped   = false;
  if constexpr (!ATS_PCRE2_HAS_ENDANCHORED) {
    if ((flags & RE_ENDANCHORED) != 0) {
      rewritten_pattern.reserve(pattern.size() + 6);
      rewritten_pattern.append("(?:").append(pattern).append(")\\z");
      effective_pattern  = rewritten_pattern;
      flags             &= ~static_cast<uint32_t>(RE_ENDANCHORED);
      pattern_wrapped    = true;
    }
  }

  PCRE2_SIZE error_offset;
  int        error_code;
  auto code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(effective_pattern.data()), effective_pattern.size(), flags, &error_code,
                            &error_offset, regex_context->get_compile_context());
  if (!code) {
    // Compensate for the "(?:" prefix so callers see offsets into their pattern, not ours.
    // If the offset is inside the prefix itself, clamp to 0.
    if (pattern_wrapped && error_offset >= 3) {
      erroroffset = static_cast<int>(error_offset - 3);
    } else {
      erroroffset = static_cast<int>(error_offset);
    }

    // get pcre2 error message
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(error_code, buffer, sizeof(buffer));
    error.assign((char const *)buffer);
    return false;
  }

  // support for JIT
  pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);

  // Replace the previous pattern only now that the new one exists. Freeing it before
  // pcre2_compile would leave every failure path above returning with a dangling
  // pointer in _code, which empty() reports as a compiled pattern and exec() hands to
  // pcre2_match.
  if (auto ptr = _Code::get(_code); ptr != nullptr) {
    pcre2_code_free(ptr);
  }

  _Code::set(_code, code);

  return true;
}

//----------------------------------------------------------------------------
bool
Regex::exec(std::string_view subject) const
{
  return this->exec(subject, 0);
}

//----------------------------------------------------------------------------
bool
Regex::exec(std::string_view subject, uint32_t flags) const
{
  if (_Code::get(_code) == nullptr) {
    return false;
  }
  RegexMatches matches;

  int count = this->exec(subject, matches, flags);
  return count >= 0;
}

//----------------------------------------------------------------------------
int32_t
Regex::exec(std::string_view subject, RegexMatches &matches) const
{
  return this->exec(subject, matches, 0);
}

//----------------------------------------------------------------------------
int32_t
Regex::exec(std::string_view subject, RegexMatches &matches, uint32_t flags, RegexMatchContext const *const matchContext) const
{
  auto code = _Code::get(_code);

  // check if there is a compiled regex
  if (code == nullptr) {
    return PCRE2_ERROR_NULL;
  }

  // Use the provided or the thread global context?
  pcre2_match_context *match_context;
  if (nullptr == matchContext) {
    match_context = RegexContext::get_instance()->get_match_context();
  } else {
    match_context = RegexMatchContext::_MatchContext::get(matchContext->_match_context);
  }

  bool const     full_match  = (flags & RE_FULL_MATCH) != 0;
  uint32_t const pcre2_flags = flags & ~RE_FULL_MATCH;

  int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), 0, pcre2_flags,
                       RegexMatches::_MatchData::get(matches._match_data), match_context);

  matches._size = rc;

  // match was successful
  if (rc >= 0) {
    matches._subject = subject;

    // match but the output vector was too small, adjust the size of the matches
    if (rc == 0) {
      matches._size = pcre2_get_ovector_count(RegexMatches::_MatchData::get(matches._match_data));
    }

    // Enforce full-subject consumption when requested.
    if (full_match && matches[0].size() != subject.size()) {
      matches._size = PCRE2_ERROR_NOMATCH;
      rc            = PCRE2_ERROR_NOMATCH;
    }
  }

  return rc;
}

//----------------------------------------------------------------------------
// static
std::string
Regex::get_error_string(int rc)
{
  std::string res;

  if (rc < 0) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(rc, buffer, sizeof(buffer));
    res.assign((char const *)buffer);
  }

  return res;
}

//----------------------------------------------------------------------------
int32_t
Regex::get_capture_count() const
{
  uint32_t captures = 0;
  if (pcre2_pattern_info(_Code::get(_code), PCRE2_INFO_CAPTURECOUNT, &captures) != 0) {
    return -1;
  }
  return static_cast<int32_t>(captures);
}

//----------------------------------------------------------------------------
int32_t
Regex::get_backref_max() const
{
  uint32_t refs = 0;
  if (pcre2_pattern_info(_Code::get(_code), PCRE2_INFO_BACKREFMAX, &refs) != 0) {
    return -1;
  }
  return static_cast<int32_t>(refs);
}

//----------------------------------------------------------------------------
bool
Regex::empty() const
{
  return _Code::get(_code) == nullptr;
}

//----------------------------------------------------------------------------
DFA::~DFA() {}

//----------------------------------------------------------------------------
bool
DFA::build(const std::string_view pattern, unsigned flags)
{
  Regex       rxp;
  std::string string{pattern};

  if (flags & RE_FULL_MATCH) {
    _full_match  = true;
    flags       &= ~RE_FULL_MATCH;
  }

  if (!(flags & RE_UNANCHORED)) {
    flags |= RE_ANCHORED;
  }

  if (!rxp.compile(pattern, flags)) {
    return false;
  }
  _patterns.emplace_back(std::move(rxp), std::move(string));
  return true;
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(const std::string_view pattern, unsigned flags)
{
  release_assert(_patterns.empty());
  this->build(pattern, flags);
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(const std::string_view *const patterns, int npatterns, unsigned flags)
{
  _patterns.reserve(npatterns); // try to pre-allocate.
  for (int i = 0; i < npatterns; ++i) {
    this->build(patterns[i], flags);
  }
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(const char *const *patterns, int npatterns, unsigned flags)
{
  _patterns.reserve(npatterns); // try to pre-allocate.
  for (int i = 0; i < npatterns; ++i) {
    this->build(patterns[i], flags);
  }
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::match(std::string_view str) const
{
  uint32_t const exec_flags = _full_match ? static_cast<uint32_t>(RE_FULL_MATCH) : 0u;

  for (auto spot = _patterns.begin(), limit = _patterns.end(); spot != limit; ++spot) {
    if (spot->_re.exec(str, exec_flags)) {
      return spot - _patterns.begin();
    }
  }

  return -1;
}
