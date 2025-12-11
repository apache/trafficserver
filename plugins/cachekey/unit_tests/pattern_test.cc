/*
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

// This file adds multiple test cases for Pattern: compile, match, capture, replace.
#include <catch2/catch_test_macros.hpp>
#include "pattern.h"

TEST_CASE("Pattern compile and match behavior", "[cachekey][pattern]")
{
  SECTION("Simple literal match")
  {
    Pattern p;
    REQUIRE(p.init("hello"));
    CHECK(p.match("hello") == true);
    CHECK(p.match("hell") == false);
  }

  SECTION("Simple capture groups")
  {
    Pattern p;
    REQUIRE(p.init("^(\\w+)-(\\d+)$"));
    StringVector caps;
    CHECK(p.capture("item-123", caps));
    // capture returns all groups including group 0, so expect 3 entries (full + 2 groups)
    CHECK(caps.size() == 3);
    CHECK(caps[1] == "item");
    CHECK(caps[2] == "123");
  }

  SECTION("Replacement using tokens")
  {
    Pattern p;
    REQUIRE(p.init("^(\\w+)-(\\d+)$", "$2:$1", /*replace*/ true));
    String res;
    CHECK(p.replace("item-123", res));
    CHECK(res == "123:item");
  }

  SECTION("Invalid pattern fails to compile")
  {
    Pattern p;
    // malformed pattern (unclosed parentheses)
    CHECK(p.init("(unclosed") == false);
  }

  SECTION("Greedy vs non-greedy capture")
  {
    Pattern pg;
    Pattern png;
    REQUIRE(pg.init("a(.*)b"));   // greedy
    REQUIRE(png.init("a(.*?)b")); // non-greedy

    StringVector caps_g;
    StringVector caps_ng;
    REQUIRE(pg.capture("a123b456b", caps_g));
    REQUIRE(png.capture("a123b456b", caps_ng));

    // greedy should capture up to the last 'b'
    CHECK(caps_g.size() >= 2);
    CHECK(caps_g[1] == "123b456");

    // non-greedy should capture up to the first 'b'
    CHECK(caps_ng.size() >= 2);
    CHECK(caps_ng[1] == "123");
  }

  SECTION("Empty-string anchors")
  {
    Pattern p;
    REQUIRE(p.init("^$"));
    // Pattern::match uses PCRE2_NOTEMPTY which prevents empty-string matches.
    // Therefore '^$' will NOT match an empty subject with the current implementation.
    CHECK(p.match("") == false);
    CHECK(p.match("not-empty") == false);
  }

  SECTION("Case-insensitive inline flag")
  {
    Pattern p;
    // PCRE2 inline flag for case-insensitive
    REQUIRE(p.init("(?i)AbC"));
    CHECK(p.match("aBc") == true);
    CHECK(p.match("ABC") == true);
  }

  SECTION("Repeated captures and empty captures")
  {
    Pattern p;
    REQUIRE(p.init("(\\w*)-(\\w*)"));
    StringVector caps;
    REQUIRE(p.capture("-foo", caps));
    CHECK(caps.size() == 3);
    // first group before '-' is empty
    CHECK(caps[1] == "");
    CHECK(caps[2] == "foo");
  }

  SECTION("Long subject match")
  {
    Pattern p;
    REQUIRE(p.init("^a+$"));
    // create a long string of 'a' characters
    std::string long_s(10000, 'a');
    CHECK(p.match(long_s.c_str()) == true);
  }

  SECTION("Config string parsing - pattern only")
  {
    Pattern p;
    REQUIRE(p.init("^test-\\d+$"));
    CHECK(p.match("test-123") == true);
    CHECK(p.match("test-abc") == false);
  }

  SECTION("Config string parsing - pattern with replacement")
  {
    Pattern p;
    REQUIRE(p.init("/^(\\w+)-(\\d+)$/$2:$1/"));
    String res;
    CHECK(p.replace("foo-42", res));
    CHECK(res == "42:foo");
  }

  SECTION("Config string parsing - escaped slashes in pattern")
  {
    Pattern p;
    REQUIRE(p.init("/path\\/to\\/file/$0/"));
    String res;
    CHECK(p.replace("path/to/file", res));
    CHECK(res == "path/to/file");
  }

  SECTION("Config string parsing - escaped slashes in replacement")
  {
    Pattern p;
    REQUIRE(p.init("/(\\w+)/prefix\\/$1/"));
    String res;
    CHECK(p.replace("test", res));
    CHECK(res == "prefix/test");
  }

  SECTION("Config string parsing - invalid format missing closing slash")
  {
    Pattern p;
    CHECK(p.init("/pattern/replacement") == false);
  }

  SECTION("Config string parsing - invalid format no slashes")
  {
    Pattern p;
    CHECK(p.init("/pattern") == false);
  }

  SECTION("Replacement with multiple groups in different order")
  {
    Pattern p;
    REQUIRE(p.init("^(\\w)(\\w)(\\w)$", "$3$1$2", true));
    String res;
    CHECK(p.replace("abc", res));
    CHECK(res == "cab");
  }

  SECTION("Replacement with group $0 (entire match)")
  {
    Pattern p;
    REQUIRE(p.init("test", "[$0]", true));
    String res;
    CHECK(p.replace("test", res));
    CHECK(res == "[test]");
  }

  SECTION("Replacement with repeated group references")
  {
    Pattern p;
    REQUIRE(p.init("(\\w+)", "$1-$1", true));
    String res;
    CHECK(p.replace("foo", res));
    CHECK(res == "foo-foo");
  }

  SECTION("Replacement with static text around groups")
  {
    Pattern p;
    REQUIRE(p.init("(\\d+)", "num=$1;", true));
    String res;
    CHECK(p.replace("123", res));
    CHECK(res == "num=123;");
  }

  SECTION("Replacement with invalid group reference")
  {
    Pattern p;
    REQUIRE(p.init("(\\w+)", "$5", true)); // only 2 groups (0 and 1)
    String res;
    // Should fail because $5 doesn't exist
    CHECK(p.replace("test", res) == false);
  }

  SECTION("process() method - capture mode (no replacement)")
  {
    Pattern p;
    REQUIRE(p.init("^(\\w+)-(\\d+)$"));
    StringVector result;
    CHECK(p.process("item-456", result));
    // process() should skip group 0 when no replacement, only return capturing groups
    CHECK(result.size() == 2);
    CHECK(result[0] == "item");
    CHECK(result[1] == "456");
  }

  SECTION("process() method - capture mode with single group")
  {
    Pattern p;
    REQUIRE(p.init("test"));
    StringVector result;
    CHECK(p.process("test", result));
    // When there's only group 0, process() returns it
    CHECK(result.size() == 1);
    CHECK(result[0] == "test");
  }

  SECTION("process() method - replace mode")
  {
    Pattern p;
    REQUIRE(p.init("/^(\\w+)-(\\d+)$/$1_$2/"));
    StringVector result;
    CHECK(p.process("foo-99", result));
    CHECK(result.size() == 1);
    CHECK(result[0] == "foo_99");
  }

  SECTION("process() method - no match")
  {
    Pattern p;
    REQUIRE(p.init("^test$"));
    StringVector result;
    CHECK(p.process("nomatch", result) == false);
    CHECK(result.size() == 0);
  }

  SECTION("Special characters in pattern")
  {
    Pattern p;
    REQUIRE(p.init("\\$\\d+\\.\\d+"));
    CHECK(p.match("$123.45") == true);
    CHECK(p.match("123.45") == false);
  }

  SECTION("Anchored patterns")
  {
    Pattern p1, p2;
    REQUIRE(p1.init("test"));   // unanchored
    REQUIRE(p2.init("^test$")); // anchored

    CHECK(p1.match("pretest") == true);
    CHECK(p2.match("pretest") == false);
    CHECK(p2.match("test") == true);
  }
}

TEST_CASE("MultiPattern tests", "[cachekey][pattern][multipattern]")
{
  SECTION("Empty multipattern")
  {
    MultiPattern mp("test");
    CHECK(mp.empty() == true);
    CHECK(mp.name() == "test");
    CHECK(mp.match("anything") == false);
  }

  SECTION("Single pattern match")
  {
    MultiPattern mp("mobile");
    auto         p = std::make_unique<Pattern>();
    REQUIRE(p->init("iPhone"));
    mp.add(std::move(p));

    CHECK(mp.empty() == false);
    CHECK(mp.match("Mozilla/5.0 (iPhone; CPU iPhone OS") == true);
    CHECK(mp.match("Mozilla/5.0 (Windows NT 10.0") == false);
  }

  SECTION("Multiple patterns - first match wins")
  {
    MultiPattern mp("devices");

    auto p1 = std::make_unique<Pattern>();
    REQUIRE(p1->init("Android"));
    mp.add(std::move(p1));

    auto p2 = std::make_unique<Pattern>();
    REQUIRE(p2->init("iPhone"));
    mp.add(std::move(p2));

    CHECK(mp.match("Android device") == true);
    CHECK(mp.match("iPhone device") == true);
    CHECK(mp.match("Windows device") == false);
  }

  SECTION("MultiPattern process with captures")
  {
    MultiPattern mp("versions");

    auto p1 = std::make_unique<Pattern>();
    REQUIRE(p1->init("Chrome/(\\d+)"));
    mp.add(std::move(p1));

    auto p2 = std::make_unique<Pattern>();
    REQUIRE(p2->init("Firefox/(\\d+)"));
    mp.add(std::move(p2));

    StringVector result;
    CHECK(mp.process("Mozilla/5.0 Chrome/91.0", result) == true);
    CHECK(result.size() >= 1);
    CHECK(result[0] == "91");

    result.clear();
    CHECK(mp.process("Mozilla/5.0 Firefox/89.0", result) == true);
    CHECK(result.size() >= 1);
    CHECK(result[0] == "89");
  }
}

TEST_CASE("NonMatchingMultiPattern tests", "[cachekey][pattern][nonmatching]")
{
  SECTION("NonMatchingMultiPattern - returns true when nothing matches")
  {
    NonMatchingMultiPattern nmp("exclude");

    auto p1 = std::make_unique<Pattern>();
    REQUIRE(p1->init("bot"));
    nmp.add(std::move(p1));

    // Should return true (no match = allowed)
    CHECK(nmp.match("normal user agent") == true);
    // Should return false (matched = not allowed)
    CHECK(nmp.match("googlebot") == false);
  }

  SECTION("NonMatchingMultiPattern - multiple exclusions")
  {
    NonMatchingMultiPattern nmp("bots");

    auto p1 = std::make_unique<Pattern>();
    REQUIRE(p1->init("bot"));
    nmp.add(std::move(p1));

    auto p2 = std::make_unique<Pattern>();
    REQUIRE(p2->init("crawler"));
    nmp.add(std::move(p2));

    CHECK(nmp.match("normal browser") == true);
    CHECK(nmp.match("googlebot") == false);
    CHECK(nmp.match("some crawler") == false);
  }
}

TEST_CASE("Classifier tests", "[cachekey][pattern][classifier]")
{
  SECTION("Empty classifier")
  {
    Classifier c;
    String     name;
    CHECK(c.classify("test", name) == false);
  }

  SECTION("Single class classification")
  {
    Classifier c;

    auto mp = std::make_unique<MultiPattern>("mobile");
    auto p1 = std::make_unique<Pattern>();
    REQUIRE(p1->init("iPhone|Android"));
    mp->add(std::move(p1));
    c.add(std::move(mp));

    String name;
    CHECK(c.classify("Mozilla/5.0 (iPhone", name) == true);
    CHECK(name == "mobile");

    CHECK(c.classify("Mozilla/5.0 (Windows", name) == false);
  }

  SECTION("Multiple classes - first match wins")
  {
    Classifier c;

    // Add mobile class first
    auto mp_mobile = std::make_unique<MultiPattern>("mobile");
    auto p1        = std::make_unique<Pattern>();
    REQUIRE(p1->init("iPhone|Android"));
    mp_mobile->add(std::move(p1));
    c.add(std::move(mp_mobile));

    // Add tablet class second
    auto mp_tablet = std::make_unique<MultiPattern>("tablet");
    auto p2        = std::make_unique<Pattern>();
    REQUIRE(p2->init("iPad"));
    mp_tablet->add(std::move(p2));
    c.add(std::move(mp_tablet));

    // Add desktop class third
    auto mp_desktop = std::make_unique<MultiPattern>("desktop");
    auto p3         = std::make_unique<Pattern>();
    REQUIRE(p3->init("Windows|Macintosh"));
    mp_desktop->add(std::move(p3));
    c.add(std::move(mp_desktop));

    String name;
    CHECK(c.classify("Mozilla/5.0 (Android", name) == true);
    CHECK(name == "mobile");

    CHECK(c.classify("Mozilla/5.0 (iPad", name) == true);
    CHECK(name == "tablet");

    CHECK(c.classify("Mozilla/5.0 (Windows NT", name) == true);
    CHECK(name == "desktop");

    CHECK(c.classify("Unknown/1.0", name) == false);
  }

  SECTION("Classifier with empty multipatterns")
  {
    Classifier c;

    // Add an empty multipattern
    auto mp = std::make_unique<MultiPattern>("empty");
    c.add(std::move(mp));

    String name;
    // Should skip empty patterns
    CHECK(c.classify("test", name) == false);
  }

  SECTION("Complex real-world classification")
  {
    Classifier c;

    // Mobile phones
    auto mp_phone = std::make_unique<MultiPattern>("phone");
    auto p1       = std::make_unique<Pattern>();
    REQUIRE(p1->init("iPhone"));
    mp_phone->add(std::move(p1));
    auto p2 = std::make_unique<Pattern>();
    REQUIRE(p2->init("Android.*Mobile"));
    mp_phone->add(std::move(p2));
    c.add(std::move(mp_phone));

    // Tablets
    auto mp_tablet = std::make_unique<MultiPattern>("tablet");
    auto p3        = std::make_unique<Pattern>();
    REQUIRE(p3->init("iPad"));
    mp_tablet->add(std::move(p3));
    auto p4 = std::make_unique<Pattern>();
    REQUIRE(p4->init("Android(?!.*Mobile)"));
    mp_tablet->add(std::move(p4));
    c.add(std::move(mp_tablet));

    String name;
    CHECK(c.classify("Mozilla/5.0 (iPhone; CPU iPhone OS 14_0", name) == true);
    CHECK(name == "phone");

    CHECK(c.classify("Mozilla/5.0 (Linux; Android 10; SM-G960U) Mobile", name) == true);
    CHECK(name == "phone");

    CHECK(c.classify("Mozilla/5.0 (iPad; CPU OS 14_0", name) == true);
    CHECK(name == "tablet");

    // Android tablet (no "Mobile" in UA)
    CHECK(c.classify("Mozilla/5.0 (Linux; Android 10; SM-T510)", name) == true);
    CHECK(name == "tablet");
  }
}
