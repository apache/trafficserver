/**
  @file Test for Regex.cc

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

#include <string_view>
#include <vector>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "tscore/ink_assert.h"
#include "tscore/ink_defs.h"
#include "tsutil/Regex.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

struct subject_match_t {
  std::string_view subject;
  bool             match;
};

struct test_t {
  std::string_view             regex;
  std::vector<subject_match_t> tests;
};

std::vector<test_t> test_data{
  {
   {{R"(^foo)"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, true}, {{"foobarbaz"}, true}}}},
   {{R"(foo$)"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, false}, {{"foobarbaz"}, false}}}},
   // url regular expression
    {{R"(^(https?:\/\/)?([\da-z\.-]+)\.([a-z\.]{2,6})([\/\w \.-]*)*\/?$)"},
     {{{{"http://www.example.com"}, true},
       {{"https://www.example.com"}, true},
       {{"http://~example.com"}, false},
       {{"http://www.example.com/foo/bar"}, true}}}},
   // ip address regular expression
    {R"(^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)",
     {{{{"1.2.3.4"}, true}, {{"127.0.0.1"}, true}, {{"256.256.256.256"}, false}, {{".1.1.1.1"}, false}}}},
   }
};

// test case insensitive test data
std::vector<test_t> test_data_case_insensitive{
  {
   {{R"(^foo)"}, {{{{"FoO"}, true}, {{"bar"}, false}, {{"foObar"}, true}, {{"foobaRbaz"}, true}}}},
   {{R"(foo$)"}, {{{{"foO"}, true}, {{"bar"}, false}, {{"foobar"}, false}, {{"foobarbaz"}, false}}}},
   }
};

// test case for anchored flag
std::vector<test_t> test_data_anchored{
  {
   {{R"(foo)"}, {{{{"foo"}, true}, {{"bar"}, false}, {{"foobar"}, true}, {{"foobarbaz"}, true}}}},
   {{R"(bar)"}, {{{{"foo"}, false}, {{"bar"}, true}, {{"foobar"}, false}, {{"foobarbaz"}, false}}}},
   }
};

struct submatch_t {
  std::string_view              subject;
  int32_t                       count;
  std::vector<std::string_view> submatches;
};

struct submatch_test_t {
  std::string_view        regex;
  int32_t                 capture_count;
  std::vector<submatch_t> tests;
};

std::vector<submatch_test_t> submatch_test_data{
  {
   {{R"(^foo)"}, 0, {{{{"foo"}, 1, {{"foo"}}}, {{"bar"}, -1, {}}, {{"foobar"}, 1, {{"foo"}}}, {{"foobarbaz"}, 1, {{"foo"}}}}}},
   {{R"(foo$)"}, 0, {{{{"foo"}, 1, {{"foo"}}}, {{"bar"}, -1, {}}, {{"foobar"}, -1, {}}, {{"foobarbaz"}, -1, {}}}}},
   {{R"(^(foo)(bar))"}, 2, {{{{"foobar"}, 3, {{"foobar", "foo", "bar"}}}, {{"barfoo"}, -1, {}}, {{"foo"}, -1, {}}}}},
   }
};

TEST_CASE("Regex", "[libts][Regex]")
{
  // case sensitive test
  for (auto &item : test_data) {
    CAPTURE(item.regex);
    Regex r;
    REQUIRE(r.compile(item.regex.data()) == true);

    for (auto &test : item.tests) {
      CAPTURE(test.subject, test.match);
      REQUIRE(r.exec(test.subject.data()) == test.match);
    }
  }

  // case insensitive test
  for (auto &item : test_data_case_insensitive) {
    CAPTURE(item.regex);
    Regex r;
    REQUIRE(r.compile(item.regex.data(), RE_CASE_INSENSITIVE) == true);

    for (auto &test : item.tests) {
      CAPTURE(test.subject, test.match);
      REQUIRE(r.exec(test.subject.data()) == test.match);
    }
  }

  // case anchored test
  for (auto &item : test_data_anchored) {
    CAPTURE(item.regex);
    Regex r;
    REQUIRE(r.compile(item.regex.data(), RE_ANCHORED) == true);

    for (auto &test : item.tests) {
      CAPTURE(test.subject, test.match);
      REQUIRE(r.exec(test.subject.data()) == test.match);
    }
  }

  // test getting submatches with operator[]
  for (auto &item : submatch_test_data) {
    CAPTURE(item.regex, item.capture_count);
    Regex r;
    REQUIRE(r.compile(item.regex.data()) == true);
    REQUIRE(r.get_capture_count() == item.capture_count);

    for (auto &test : item.tests) {
      CAPTURE(test.subject, test.count);
      RegexMatches matches;
      REQUIRE(r.exec(test.subject.data(), matches) == test.count);
      REQUIRE(matches.size() == test.count);

      for (int32_t i = 0; i < test.count; i++) {
        REQUIRE(matches[i] == test.submatches[i]);
      }
    }
  }

  // test getting submatches with ovector pointer
  for (auto &item : submatch_test_data) {
    CAPTURE(item.regex, item.capture_count);
    Regex r;
    REQUIRE(r.compile(item.regex.data()) == true);
    REQUIRE(r.get_capture_count() == item.capture_count);

    for (auto &test : item.tests) {
      CAPTURE(test.subject, test.count);
      RegexMatches matches;
      REQUIRE(r.exec(test.subject.data(), matches) == test.count);
      REQUIRE(matches.size() == test.count);

      size_t *ovector = matches.get_ovector_pointer();
      for (int32_t i = 0; i < test.count; i++) {
        REQUIRE(test.submatches[i] == std::string_view{test.subject.data() + ovector[i * 2], ovector[i * 2 + 1] - ovector[i * 2]});
      }
    }
  }

  // test for invalid regular expression
  {
    Regex r;
    REQUIRE(r.compile(R"((\d+)", RE_CASE_INSENSITIVE) == false);
  }

  // test for not compiling regular expression
  {
    Regex        r;
    RegexMatches matches;
    REQUIRE(r.exec("foo") == false);
    REQUIRE(r.exec("foo", matches) == RE_ERROR_NULL);
  }

  // test for recompiling the regular expression
  {
    Regex r;
    REQUIRE(r.compile(R"(foo)") == true);
    REQUIRE(r.exec("foo") == true);
    REQUIRE(r.compile(R"(bar)") == true);
    REQUIRE(r.exec("bar") == true);
  }

// test with matches set to 100, don't run the test in debug mode or the test will abort with a message to increase the buffer size
#ifndef DEBUG
  {
    Regex        r;
    RegexMatches matches(100);
    REQUIRE(r.compile(R"(foo)") == true);
    REQUIRE(r.exec("foo", matches) == 1);
  }
#endif
}

TEST_CASE("Regex RE_NOTEMPTY flag behavior", "[libts][Regex][flags][RE_NOTEMPTY]")
{
  // Pattern that only matches empty string
  Regex r;
  REQUIRE(r.compile("^$") == true);

  SECTION("default exec matches empty subject")
  {
    // boolean overload
    CHECK(r.exec(std::string_view("")) == true);

    // matches overload should return 1 (one match - the whole subject)
    RegexMatches matches;
    CHECK(r.exec(std::string_view(""), matches) == 1);
    CHECK(matches.size() == 1);
    CHECK(matches[0] == std::string_view(""));
  }

  SECTION("RE_NOTEMPTY prevents empty matches")
  {
    // boolean overload with RE_NOTEMPTY should not match
    CHECK(r.exec(std::string_view(""), RE_NOTEMPTY) == false);

    // matches overload should return RE_ERROR_NOMATCH
    RegexMatches matches;
    int          rc = r.exec(std::string_view(""), matches, RE_NOTEMPTY);
    CHECK(rc == RE_ERROR_NOMATCH);
  }

  SECTION("non-empty subject unaffected by RE_NOTEMPTY for this pattern")
  {
    // '^$' should not match 'a' in any case
    CHECK(r.exec(std::string_view("a")) == false);
    CHECK(r.exec(std::string_view("a"), RE_NOTEMPTY) == false);
  }
}

TEST_CASE("Regex error codes", "[libts][Regex][errors]")
{
  SECTION("RE_ERROR_NULL when regex not compiled")
  {
    Regex        r;
    RegexMatches matches;

    // exec on uncompiled regex should return RE_ERROR_NULL
    CHECK(r.exec("test", matches) == RE_ERROR_NULL);
  }

  SECTION("RE_ERROR_NOMATCH when pattern does not match")
  {
    Regex r;
    REQUIRE(r.compile(R"(^foo$)") == true);

    RegexMatches matches;

    // Pattern does not match, should return RE_ERROR_NOMATCH
    CHECK(r.exec("bar", matches) == RE_ERROR_NOMATCH);
    CHECK(r.exec("foobar", matches) == RE_ERROR_NOMATCH);
    CHECK(r.exec("", matches) == RE_ERROR_NOMATCH);

    // The following should match and return 1 (which shouldn't be RE_ERROR_NOMATCH)
    CHECK(r.exec("foo", matches) != RE_ERROR_NOMATCH);
    CHECK(r.exec("foo", matches) == 1);
  }

  SECTION("Compile error returns detailed error message")
  {
    Regex       r;
    std::string error;
    int         erroffset;

    // Unclosed parenthesis should fail with error message
    CHECK(r.compile(R"((unclosed)", error, erroffset) == false);
    CHECK(!error.empty());
    CHECK(erroffset > 0);

    // Invalid escape sequence
    error.clear();
    erroffset = 0;
    CHECK(r.compile(R"(\k)", error, erroffset) == false);
    CHECK(!error.empty());

    // Invalid character class
    error.clear();
    erroffset = 0;
    CHECK(r.compile(R"([z-a])", error, erroffset) == false);
    CHECK(!error.empty());
  }
}

TEST_CASE("Regex::empty()", "[libts][Regex][empty]")
{
  SECTION("newly constructed Regex is empty")
  {
    Regex r;
    CHECK(r.empty() == true);
  }

  SECTION("compiled Regex is not empty")
  {
    Regex r;
    REQUIRE(r.compile("test") == true);
    CHECK(r.empty() == false);
  }

  SECTION("failed compilation leaves Regex empty")
  {
    Regex r;
    REQUIRE(r.compile("(invalid") == false);
    CHECK(r.empty() == true);
  }

  SECTION("recompiling non-empty Regex")
  {
    Regex r;
    REQUIRE(r.compile("foo") == true);
    CHECK(r.empty() == false);

    REQUIRE(r.compile("bar") == true);
    CHECK(r.empty() == false);
  }
}

TEST_CASE("Regex move semantics", "[libts][Regex][move]")
{
  SECTION("move constructor")
  {
    Regex r1;
    REQUIRE(r1.compile("^test$") == true);
    CHECK(r1.exec("test") == true);
    CHECK(r1.empty() == false);

    // Move construct r2 from r1
    Regex r2(std::move(r1));
    CHECK(r2.empty() == false);
    CHECK(r2.exec("test") == true);
    CHECK(r2.exec("foo") == false);
  }

  SECTION("move assignment operator")
  {
    Regex r1;
    REQUIRE(r1.compile("^test$") == true);

    Regex r2;
    REQUIRE(r2.compile("^foo$") == true);

    // Move assign r1 to r2
    r2 = std::move(r1);
    CHECK(r2.empty() == false);
    CHECK(r2.exec("test") == true);
    CHECK(r2.exec("foo") == false);
  }

  SECTION("move empty Regex")
  {
    Regex r1; // empty
    Regex r2(std::move(r1));
    CHECK(r2.empty() == true);
  }
}

TEST_CASE("Regex RE_UNANCHORED flag", "[libts][Regex][flags][RE_UNANCHORED]")
{
  SECTION("RE_UNANCHORED allows matching anywhere in multiline text")
  {
    Regex r;
    // Pattern that should match "test" at start of any line
    REQUIRE(r.compile("^test", RE_UNANCHORED) == true);

    // Should match at start of string
    CHECK(r.exec("test\nfoo") == true);

    // Should match after newline (multiline mode)
    CHECK(r.exec("foo\ntest") == true);

    // Should not match in middle of line
    CHECK(r.exec("foo test") == false);
  }

  SECTION("default (without RE_UNANCHORED) only matches at string start")
  {
    Regex r;
    REQUIRE(r.compile("^test") == true);

    // Should match at start
    CHECK(r.exec("test\nfoo") == true);

    // Should NOT match after newline without RE_UNANCHORED
    CHECK(r.exec("foo\ntest") == false);
  }
}

TEST_CASE("RegexMatches edge cases", "[libts][Regex][RegexMatches]")
{
  SECTION("RegexMatches size after no match")
  {
    Regex        r;
    RegexMatches matches;
    REQUIRE(r.compile("test") == true);

    int count = r.exec("nomatch", matches);
    CHECK(count == RE_ERROR_NOMATCH);
    CHECK(matches.size() == RE_ERROR_NOMATCH);
  }

  SECTION("RegexMatches operator[] with various capture counts")
  {
    Regex r;
    REQUIRE(r.compile("(\\w+)-(\\d+)-(\\w+)") == true);

    RegexMatches matches;
    int          count = r.exec("foo-123-bar", matches);
    CHECK(count == 4); // whole match + 3 groups

    CHECK(matches[0] == "foo-123-bar");
    CHECK(matches[1] == "foo");
    CHECK(matches[2] == "123");
    CHECK(matches[3] == "bar");
  }

  SECTION("RegexMatches with zero-length captures")
  {
    Regex r;
    REQUIRE(r.compile("(\\w*)-(\\w*)") == true);

    RegexMatches matches;

    // First group empty, second has content
    int count = r.exec("-foo", matches);
    CHECK(count == 3);
    CHECK(matches[0] == "-foo");
    CHECK(matches[1] == "");
    CHECK(matches[2] == "foo");
  }

  SECTION("RegexMatches with optional groups")
  {
    Regex r;
    REQUIRE(r.compile("(\\w+)-(\\d+)?") == true);

    RegexMatches matches;

    // With optional group present
    int count = r.exec("foo-123", matches);
    CHECK(count == 3);
    CHECK(matches[1] == "foo");
    CHECK(matches[2] == "123");

    // With optional group absent - note: PCRE2 may still count it
    count = r.exec("foo-", matches);
    CHECK(count >= 2); // At least whole match + first group
    CHECK(matches[1] == "foo");
  }
}

TEST_CASE("Regex with special characters", "[libts][Regex][special]")
{
  SECTION("escaped special characters")
  {
    Regex r;
    REQUIRE(r.compile(R"(\$\d+\.\d+)") == true);

    CHECK(r.exec("$123.45") == true);
    CHECK(r.exec("123.45") == false);
    CHECK(r.exec("$12.3") == true);
  }

  SECTION("character classes")
  {
    Regex r;
    REQUIRE(r.compile(R"([A-Z][a-z]+)") == true);

    CHECK(r.exec("Hello") == true);
    CHECK(r.exec("hello") == false);
    CHECK(r.exec("HELLO") == false);
  }

  SECTION("quantifiers")
  {
    Regex r;
    REQUIRE(r.compile(R"(\d{3}-\d{4})") == true);

    CHECK(r.exec("123-4567") == true);
    CHECK(r.exec("12-4567") == false);
    CHECK(r.exec("123-456") == false);
  }

  SECTION("alternation")
  {
    Regex r;
    REQUIRE(r.compile(R"(foo|bar|baz)") == true);

    CHECK(r.exec("foo") == true);
    CHECK(r.exec("bar") == true);
    CHECK(r.exec("baz") == true);
    CHECK(r.exec("qux") == false);
  }
}

TEST_CASE("Regex with complex patterns", "[libts][Regex][complex]")
{
  SECTION("greedy vs non-greedy quantifiers")
  {
    Regex greedy, non_greedy;
    REQUIRE(greedy.compile(R"(<.*>)") == true);
    REQUIRE(non_greedy.compile(R"(<.*?>)") == true);

    RegexMatches matches;

    // Greedy matches everything
    int count = greedy.exec("<div>content</div>", matches);
    CHECK(count == 1);
    CHECK(matches[0] == "<div>content</div>");

    // Non-greedy matches just first tag
    count = non_greedy.exec("<div>content</div>", matches);
    CHECK(count == 1);
    CHECK(matches[0] == "<div>");
  }

  SECTION("lookahead assertions")
  {
    Regex r;
    // Match "foo" only if followed by "bar"
    REQUIRE(r.compile(R"(foo(?=bar))") == true);

    CHECK(r.exec("foobar") == true);
    CHECK(r.exec("foobaz") == false);
    CHECK(r.exec("foo") == false);
  }

  SECTION("negative lookahead")
  {
    Regex r;
    // Match "foo" only if NOT followed by "bar"
    REQUIRE(r.compile(R"(foo(?!bar))") == true);

    CHECK(r.exec("foobar") == false);
    CHECK(r.exec("foobaz") == true);
    CHECK(r.exec("foo") == true);
  }

  SECTION("word boundaries")
  {
    Regex r;
    REQUIRE(r.compile(R"(\btest\b)") == true);

    CHECK(r.exec("test") == true);
    CHECK(r.exec("a test here") == true);
    CHECK(r.exec("testing") == false);
    CHECK(r.exec("attest") == false);
  }
}

TEST_CASE("Regex recompilation behavior", "[libts][Regex][recompile]")
{
  SECTION("recompile frees previous pattern")
  {
    Regex r;

    REQUIRE(r.compile("foo") == true);
    CHECK(r.exec("foo") == true);
    CHECK(r.exec("bar") == false);

    // Recompile with different pattern
    REQUIRE(r.compile("bar") == true);
    CHECK(r.exec("bar") == true);
    CHECK(r.exec("foo") == false);
  }

  SECTION("recompile after failed compilation")
  {
    Regex r;

    // First compilation fails
    REQUIRE(r.compile("(invalid") == false);
    CHECK(r.empty() == true);

    // Should still be able to compile successfully
    REQUIRE(r.compile("valid") == true);
    CHECK(r.empty() == false);
    CHECK(r.exec("valid") == true);
  }

  SECTION("recompile with different flags")
  {
    Regex r;

    REQUIRE(r.compile("test") == true);
    CHECK(r.exec("TEST") == false);

    // Recompile with case insensitive flag
    REQUIRE(r.compile("test", RE_CASE_INSENSITIVE) == true);
    CHECK(r.exec("TEST") == true);
  }
}

TEST_CASE("Regex copy constructor", "[libts][Regex][copy]")
{
  SECTION("Copy constructor creates independent copy")
  {
    Regex original;
    REQUIRE(original.compile(R"(^test\d+$)") == true);

    // Test original works
    CHECK(original.exec("test123") == true);
    CHECK(original.exec("test") == false);

    // Copy using copy constructor
    Regex copy(original);

    // Both should work independently
    CHECK(copy.exec("test123") == true);
    CHECK(copy.exec("test") == false);
    CHECK(original.exec("test456") == true);
    CHECK(original.exec("test") == false);
  }

  SECTION("Copy constructor with capture groups")
  {
    Regex original;
    REQUIRE(original.compile(R"(^(\w+)@(\w+)\.(\w+)$)") == true);

    Regex copy(original);

    // Test both original and copy with captures
    RegexMatches original_matches;
    REQUIRE(original.exec("user@example.com", original_matches) == 4);
    CHECK(original_matches[0] == "user@example.com");
    CHECK(original_matches[1] == "user");
    CHECK(original_matches[2] == "example");
    CHECK(original_matches[3] == "com");

    RegexMatches copy_matches;
    REQUIRE(copy.exec("admin@test.org", copy_matches) == 4);
    CHECK(copy_matches[0] == "admin@test.org");
    CHECK(copy_matches[1] == "admin");
    CHECK(copy_matches[2] == "test");
    CHECK(copy_matches[3] == "org");
  }

  SECTION("Copy constructor with empty regex")
  {
    Regex original; // Not compiled
    Regex copy(original);

    // Both should be empty
    CHECK(original.empty() == true);
    CHECK(copy.empty() == true);

    // Neither should match anything
    CHECK(original.exec("test") == false);
    CHECK(copy.exec("test") == false);
  }

  SECTION("Copy constructor with case insensitive flag")
  {
    Regex original;
    REQUIRE(original.compile(R"(^FOO$)", RE_CASE_INSENSITIVE) == true);

    Regex copy(original);

    // Both should match case-insensitively
    CHECK(original.exec("foo") == true);
    CHECK(original.exec("FOO") == true);
    CHECK(original.exec("FoO") == true);
    CHECK(copy.exec("foo") == true);
    CHECK(copy.exec("FOO") == true);
    CHECK(copy.exec("FoO") == true);
  }

  SECTION("Multiple copies can coexist")
  {
    Regex original;
    REQUIRE(original.compile(R"(\d+)") == true);

    Regex copy1(original);
    Regex copy2(original);
    Regex copy3(copy1);

    // All should work independently
    CHECK(original.exec("123") == true);
    CHECK(copy1.exec("456") == true);
    CHECK(copy2.exec("789") == true);
    CHECK(copy3.exec("000") == true);
  }

  SECTION("Copy can be stored in vector")
  {
    Regex pattern;
    REQUIRE(pattern.compile(R"(test\d+)") == true);

    std::vector<Regex> patterns;
    patterns.push_back(pattern);
    patterns.push_back(pattern);
    patterns.push_back(pattern);

    // All copies in vector should work
    for (auto &p : patterns) {
      CHECK(p.exec("test123") == true);
      CHECK(p.exec("test") == false);
    }
  }
}

TEST_CASE("Regex copy assignment", "[libts][Regex][copy]")
{
  SECTION("Copy assignment replaces existing pattern")
  {
    Regex regex1;
    Regex regex2;

    REQUIRE(regex1.compile(R"(foo)") == true);
    REQUIRE(regex2.compile(R"(bar)") == true);

    CHECK(regex1.exec("foo") == true);
    CHECK(regex1.exec("bar") == false);
    CHECK(regex2.exec("foo") == false);
    CHECK(regex2.exec("bar") == true);

    // Copy assign regex1 to regex2
    regex2 = regex1;

    // Now both should match "foo"
    CHECK(regex1.exec("foo") == true);
    CHECK(regex1.exec("bar") == false);
    CHECK(regex2.exec("foo") == true);
    CHECK(regex2.exec("bar") == false);
  }

  SECTION("Copy assignment from empty regex")
  {
    Regex compiled;
    Regex empty;

    REQUIRE(compiled.compile(R"(test)") == true);
    CHECK(compiled.exec("test") == true);

    // Assign empty to compiled
    compiled = empty;

    // Now compiled should be empty
    CHECK(compiled.empty() == true);
    CHECK(compiled.exec("test") == false);
  }

  SECTION("Copy assignment to empty regex")
  {
    Regex empty;
    Regex compiled;

    REQUIRE(compiled.compile(R"(test)") == true);

    // Assign compiled to empty
    empty = compiled;

    // Now empty should work
    CHECK(empty.exec("test") == true);
    CHECK(compiled.exec("test") == true);
  }

  SECTION("Self-assignment is safe")
  {
    Regex regex;
    REQUIRE(regex.compile(R"(test)") == true);

    // Self-assign (disable warning for intentional self-assignment test)
    // Use a pointer indirection to avoid compiler warnings about self-assignment
    Regex *ptr = &regex;
    regex      = *ptr;

    // Should still work
    CHECK(regex.exec("test") == true);
    CHECK(regex.exec("foo") == false);
  }

  SECTION("Copy assignment with capture groups")
  {
    Regex regex1;
    Regex regex2;

    REQUIRE(regex1.compile(R"(^(\d{3})-(\d{3})-(\d{4})$)") == true);
    REQUIRE(regex2.compile(R"(foo)") == true);

    regex2 = regex1;

    RegexMatches matches;
    REQUIRE(regex2.exec("123-456-7890", matches) == 4);
    CHECK(matches[0] == "123-456-7890");
    CHECK(matches[1] == "123");
    CHECK(matches[2] == "456");
    CHECK(matches[3] == "7890");
  }

  SECTION("Copy assignment chain")
  {
    Regex r1, r2, r3;
    REQUIRE(r1.compile(R"(test\d+)") == true);

    // Chain assignment
    r3 = r2 = r1;

    // All should work
    CHECK(r1.exec("test123") == true);
    CHECK(r2.exec("test456") == true);
    CHECK(r3.exec("test789") == true);
  }
}

TEST_CASE("Regex copy with RE_NOTEMPTY flag", "[libts][Regex][copy][flags]")
{
  SECTION("Copied regex preserves RE_NOTEMPTY behavior")
  {
    Regex original;
    REQUIRE(original.compile("^$") == true);

    Regex copy(original);

    // Both should have same behavior with RE_NOTEMPTY
    CHECK(original.exec(std::string_view("")) == true);
    CHECK(original.exec(std::string_view(""), RE_NOTEMPTY) == false);

    CHECK(copy.exec(std::string_view("")) == true);
    CHECK(copy.exec(std::string_view(""), RE_NOTEMPTY) == false);
  }
}

struct backref_test_t {
  std::string_view regex;
  bool             valid;
  int32_t          backref_max;
};

std::vector<backref_test_t> backref_test_data{
  {{""},                  true,  0 },
  {{R"(\b(\w+)\s+\1\b)"}, true,  1 },
  {{R"((.)\1)"},          true,  1 },
  {{R"((.)(.).\2\1)"},    true,  2 },
  {{R"((.\2\1)"},         false, -1},
};

TEST_CASE("Regex back reference counting", "[libts][Regex][get_backref_max]")
{
  auto item = GENERATE(from_range(backref_test_data));
  CAPTURE(item.regex, item.valid, item.backref_max);
  Regex r;
  REQUIRE(r.compile(item.regex) == item.valid);
  REQUIRE(r.get_backref_max() == item.backref_max);
}

struct match_context_test_t {
  std::string_view regex;
  std::string_view str;
  bool             valid;
  int32_t          rcode;
};

std::vector<match_context_test_t> match_context_test_data{
  {{"abc"},                          {"abc"},          true,  1  },
  {{"abc"},                          {"a"},            true,  -1 },
  {{R"(^(\d{3})-(\d{3})-(\d{4})$)"}, {"123-456-7890"}, true,  -47},
  {{"(."},                           {"a"},            false, -51},
};

TEST_CASE("RegexMatchContext", "[libts][Regex][RegexMatchContext]")
{
  RegexMatchContext match_context;
  match_context.set_match_limit(2);
  RegexMatches matches;

  auto item = GENERATE(from_range(match_context_test_data));
  CAPTURE(item.regex, item.str, item.valid, item.rcode);
  Regex r;
  REQUIRE(r.compile(item.regex) == item.valid);
  REQUIRE(r.exec(item.str, matches, 0, &match_context) == item.rcode);
}
