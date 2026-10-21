/** @file

  A brief file description

  @par License

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

#include <array>
#include <string>
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
    if (_jit_stack != nullptr) {
      pcre2_jit_stack_free(_jit_stack);
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
    _jit_stack       = pcre2_jit_stack_create(4096, 1024 * 1024, nullptr); // 1 page min and 1MB max
    pcre2_jit_stack_assign(_match_context, nullptr, _jit_stack);
  }
  pcre2_general_context *_general_context = nullptr;
  pcre2_compile_context *_compile_context = nullptr;
  pcre2_match_context   *_match_context   = nullptr;
  pcre2_jit_stack       *_jit_stack       = nullptr;
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
  auto ctx = pcre2_match_context_create(nullptr);
  debug_assert_message(ctx, "Failed to allocate custom pcre2 match context");
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
    if (nullptr != ptr) {
      pcre2_match_context *const ctx = pcre2_match_context_copy(ptr);
      _MatchContext::set(_match_context, ctx);
    } else {
      _MatchContext::set(_match_context, nullptr);
    }
  }
  return *this;
}

//----------------------------------------------------------------------------
RegexMatchContext::~RegexMatchContext()
{
  auto ptr = _MatchContext::get(_match_context);
  debug_assert_message(ptr, "Failed to get the match context");
  if (ptr != nullptr) {
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
int32_t                        Regex::policy_gate_anchoring      = 0;
Regex::PolicyGateReportHandler Regex::policy_gate_report_handler = nullptr;

namespace
{
/** Is every top-level alternative of @a pattern anchored at both ends?
 *
 * PCRE2 cannot answer this. @c PCRE2_INFO_ALLOPTIONS reports @c PCRE2_ANCHORED whenever
 * every alternative is start-anchored, and reports nothing whatever about the end --
 * @c "^foo", @c "^foo$" and @c "^foo\z" are indistinguishable through every info field
 * it offers. So the end has to be read from the pattern.
 *
 * Per alternative, because a check on the pattern's last character only sees the last
 * one: @c "^a|^b$" ends in @c $ and reports @c PCRE2_ANCHORED, yet still matches
 * @c "a-and-more".
 *
 * Conservative by construction -- anything unaccounted for returns @c false, so the
 * pattern is reported rather than silently cleared. An extra report costs a log line; a
 * missing one silently changes a rule when enforcement is enabled.
 */
bool
every_alternative_anchored(std::string_view pattern)
{
  // A backtracking verb can end the match before a trailing anchor is reached,
  // (*ACCEPT) most obviously, so do not reason about anchoring in its presence.
  if (pattern.empty() || pattern.find("(*") != std::string_view::npos) {
    return false;
  }

  auto anchored = [](std::string_view alt) {
    if (!alt.starts_with("^") && !alt.starts_with("\\A")) {
      return false;
    }

    size_t const anchor = alt.ends_with("\\z") ? 2 : (alt.ends_with("$") ? 1 : 0);

    if (anchor == 0) {
      return false;
    }

    // An odd number of backslashes before it makes the anchor a literal.
    size_t backslashes = 0;

    for (size_t i = alt.size() - anchor; i > 0 && alt[i - 1] == '\\'; --i) {
      ++backslashes;
    }
    return (backslashes % 2) == 0;
  };

  size_t depth    = 0;
  size_t begin    = 0;
  bool   in_class = false;

  for (size_t i = 0; i < pattern.size(); ++i) {
    char const c = pattern[i];

    if (c == '\\') {
      ++i; // whatever follows is a literal, including '|', '[', '(' and ')'
      continue;
    }
    if (in_class) {
      in_class = (c != ']');
      continue;
    }
    if (c == '[') {
      in_class = true;
    } else if (c == '(') {
      ++depth;
    } else if (c == ')') {
      if (depth == 0) {
        return false; // unbalanced as far as this scan can tell
      }
      --depth;
    } else if (c == '|' && depth == 0) {
      if (!anchored(pattern.substr(begin, i - begin))) {
        return false;
      }
      begin = i + 1;
    }
  }

  return !in_class && depth == 0 && anchored(pattern.substr(begin));
}

/** Tell the operator what the anchoring policy did, or would do, to a gate pattern.
 *
 * Silent when the pattern already requires a whole-subject match, whether the caller
 * asked for it through the compile flags or wrote the anchors into every alternative.
 * The common, correct ``^...$`` therefore produces nothing: reporting it would put a
 * line in the log for most rules in a typical configuration, and a report that fires
 * on the form operators are supposed to use is one they learn to ignore.
 *
 * The cost is that ``$`` is marginally weaker than @c PCRE2_ENDANCHORED -- it is also
 * satisfied immediately before a final newline, and a subject can carry one, since
 * @c unescapifyStr() decodes @c %0A in a request URL. Such a rule can still change
 * under enforcement without having been reported, which the migration guidance states
 * rather than trying to encode into a rewrite.
 *
 * Both modes report, so a gate rule added long after the migration is named either way
 * rather than silently becoming inert under enforcement.
 */
void
report_policy_gate(std::string_view pattern, uint32_t caller_flags, std::string_view policy_gate, bool enforcing)
{
  if (Regex::policy_gate_report_handler == nullptr) { // nobody listening
    return;
  }

  // Silent when the pattern already requires a whole-subject match, either because the
  // caller compiled for one or because it is anchored in the pattern itself.
  if ((caller_flags & (RE_ANCHORED | RE_ENDANCHORED)) == (RE_ANCHORED | RE_ENDANCHORED) || every_alternative_anchored(pattern)) {
    return;
  }

  std::string message{"[regex-anchor] "};

  message.append(policy_gate).append(" pattern '").append(pattern);

  if (enforcing) {
    message.append("' anchored to the whole subject by policy. ");
  } else {
    message.append("' is not anchored to the whole subject. ");
  }
  message.append("See admin-guide/configuration/regex-best-practices.");

  Regex::policy_gate_report_handler(enforcing ? Regex::PolicyGateReport::ENFORCED : Regex::PolicyGateReport::UNENFORCED, message);
}
} // namespace

//----------------------------------------------------------------------------
// The overloads without a policy_gate parameter exist so that the mangled names
// plugins were built against keep resolving. A defaulted parameter is supplied at
// the call site, so appending one to the declarations here would rename the symbol
// and every already-built plugin linking Regex::compile would fail to load.
bool
Regex::compile(std::string_view pattern, uint32_t flags)
{
  return this->compile(pattern, flags, std::string_view{});
}

//----------------------------------------------------------------------------
bool
Regex::compile(std::string_view pattern, uint32_t flags, std::string_view policy_gate)
{
  std::string error;
  int         erroroffset;

  return this->compile(pattern, error, erroroffset, flags, policy_gate);
}

//----------------------------------------------------------------------------
bool
Regex::compile(std::string_view pattern, std::string &error, int &erroroffset, uint32_t flags)
{
  return this->compile(pattern, error, erroroffset, flags, std::string_view{});
}

//----------------------------------------------------------------------------
bool
Regex::compile(std::string_view pattern, std::string &error, int &erroroffset, uint32_t flags, std::string_view policy_gate)
{
  // get the RegexContext instance - should only be null when shutting down
  RegexContext *regex_context = RegexContext::get_instance();
  if (regex_context == nullptr) {
    return false;
  }

  // Read the policy exactly once. The records subsystem writes it from another thread,
  // and a 0 -> 1 transition landing between the anchor decision and the report decision
  // would compile the pattern unanchored and also suppress the report about it, which is
  // the one state this is meant to prevent.
  bool const    is_gate   = !policy_gate.empty();
  int32_t const anchoring = policy_gate_anchoring;

  // What the caller asked for, before the policy or the old-PCRE2 shim below change it.
  // The report distinguishes "the caller already required a whole-subject match" from
  // "the policy added it", and after the OR below those are indistinguishable.
  uint32_t const caller_flags = flags;

  // Anchor gate patterns to the whole subject when the policy is on.
  if (is_gate && anchoring) {
    flags |= RE_ANCHORED | RE_ENDANCHORED;
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

  // Report either way: warn-mode patterns need auditing, and enforced ones need a record
  // that the policy touched them, so a rule added after the migration is not silent.
  if (is_gate) {
    report_policy_gate(pattern, caller_flags, policy_gate, anchoring != 0);
  }

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
