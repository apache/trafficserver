/** @file

   Catch-based unit tests for URL

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <cstdio>

#include "catch.hpp"

#include "URL.h"

TEST_CASE("Validate Scheme", "[proxy][validscheme]")
{
  static const struct {
    std::string_view text;
    bool valid;
  } scheme_test_cases[] = {{"http", true},      {"https", true},      {"example", true},    {"example.", true},
                           {"example++", true}, {"example--.", true}, {"++example", false}, {"--example", false},
                           {".example", false}, {"example://", false}};

  for (auto i : scheme_test_cases) {
    // it's pretty hard to debug with
    //     CHECK(validate_scheme(i.text) == i.valid);

    std::string_view text = i.text;
    if (validate_scheme(text) != i.valid) {
      std::printf("Validation of scheme: \"%s\", expected %s, but not\n", text.data(), (i.valid ? "true" : "false"));
      CHECK(false);
    }
  }
}

struct get_hash_test_case {
  const std::string description;
  const std::string uri_1;
  const std::string uri_2;
  const bool has_equal_hash;
};

constexpr bool HAS_EQUAL_HASH = true;

// clang-format off
std::vector<get_hash_test_case> get_hash_test_cases = {
  {
    "No encoding: equal hashes",
    "http://one.example.com/a/path?name=value#some=value?with_question#fragment",
    "http://one.example.com/a/path?name=value#some=value?with_question#fragment",
    HAS_EQUAL_HASH,
  },
  {
    "Scheme encoded: equal hashes",
    "http%3C://one.example.com/a/path?name=value#some=value?with_question#fragment",
    "http<://one.example.com/a/path?name=value#some=value?with_question#fragment",
    HAS_EQUAL_HASH,
  },
  {
    "Host encoded: equal hashes",
    "http://one%2Eexample.com/a/path?name=value#some=value?with_question#fragment",
    "http://one.example.com/a/path?name=value#some=value?with_question#fragment",
    HAS_EQUAL_HASH,
  },
  {
    "Path encoded: differing hashes",
    "http://one.example.com/a%2Fpath?name=value#some=value?with_question#fragment",
    "http://one.example.com/a/path?name=value#some=value?with_question#fragment",
    !HAS_EQUAL_HASH,
  },
  {
    "Query = encoded: differing hashes",
    "http://one.example.com/a/path?name%3Dvalue#some=value?with_question#fragment",
    "http://one.example.com/a/path?name=value#some=value?with_question#fragment",
    !HAS_EQUAL_HASH,
  },
  {
    "Query internal encoded: differing hashes",
    "http://one.example.com/a/path?name=valu%5D#some=value?with_question#fragment",
    "http://one.example.com/a/path?name=valu]#some=value?with_question#fragment",
    !HAS_EQUAL_HASH,
  },
  {
    "Fragment encoded: fragment is not part of the hash",
    "http://one.example.com/a/path?name=value#some=value?with_question#frag%7Dent",
    "http://one.example.com/a/path?name=value#some=value?with_question/frag}ent",
    HAS_EQUAL_HASH,
  },
  {
    "Username encoded: equal hashes",
    "mysql://my%7Eser:mypassword@localhost/mydatabase",
    "mysql://my~ser:mypassword@localhost/mydatabase",
    HAS_EQUAL_HASH,
  },
  {
    "Password encoded: equal hashes",
    "mysql://myuser:mypa%24sword@localhost/mydatabase",
    "mysql://myuser:mypa$sword@localhost/mydatabase",
    HAS_EQUAL_HASH,
  },
};

/** Return the hash related to a URI.
  *
  * @param[in] uri The URI to hash.
  * @return The hash of the URI.
 */
CryptoHash
get_hash(const std::string &uri)
{
  URL url;
  HdrHeap *heap = new_HdrHeap();
  url.create(heap);
  url.parse(uri.c_str(), uri.length());
  CryptoHash hash;
  url.hash_get(&hash);
  heap->destroy();
  return hash;
}

TEST_CASE("UrlHashGet", "[url][hash_get]")
{
  for (auto const &test_case : get_hash_test_cases) {
    std::string description = test_case.description + ": " + test_case.uri_1 + " vs " + test_case.uri_2;
    SECTION(description) {
      CryptoHash hash1 = get_hash(test_case.uri_1);
      CryptoHash hash2 = get_hash(test_case.uri_2);
      if (test_case.has_equal_hash) {
        CHECK(hash1 == hash2);
      } else {
        CHECK(hash1 != hash2);
      }
    }
  }
}
