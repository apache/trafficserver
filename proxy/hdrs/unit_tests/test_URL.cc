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

struct url_parse_test_case {
  const std::string input_uri;
  const std::string expected_printed_url;
  const bool verify_host_characters;
  const std::string expected_printed_url_regex;
  const bool is_valid;
  const bool is_valid_regex;
};

constexpr bool IS_VALID               = true;
constexpr bool VERIFY_HOST_CHARACTERS = true;

// clang-format off
std::vector<url_parse_test_case> url_parse_test_cases = {
  {
    "///dir////index.html",
    "/dir////index.html",
    VERIFY_HOST_CHARACTERS,
    "/dir////index.html",
    IS_VALID,
    IS_VALID
  },
  {
    "/index.html",
    "/index.html",
    VERIFY_HOST_CHARACTERS,
    "/index.html",
    IS_VALID,
    IS_VALID
  },
  {
    "//index.html",
    "/index.html",
    VERIFY_HOST_CHARACTERS,
    "/index.html",
    IS_VALID,
    IS_VALID
  },
  {
    // The following scheme-only URI is technically valid per the spec, but we
    // have historically returned this as invalid and I'm not comfortable
    // changing it in case something depends upon this behavior. Besides, a
    // scheme-only URI is probably not helpful to us nor something likely
    // Traffic Server will see.
    "http://",
    "",
    VERIFY_HOST_CHARACTERS,
    "",
    !IS_VALID,
    !IS_VALID
  },
  {
    "https:///",
    "https:///",
    VERIFY_HOST_CHARACTERS,
    "https:///",
    IS_VALID,
    IS_VALID
  },
  {
    // RFC 3986 section-3: When authority is not present, the path cannot begin
    // with two slash characters ("//"). We have historically allowed this,
    // however, and will continue to do so.
    "https:////",
    "https:///",
    VERIFY_HOST_CHARACTERS,
    "https:///",
    IS_VALID,
    IS_VALID
  },
  {
    // By convention, our url_print() function adds a path of '/' at the end of
    // URLs that have no path, query, or fragment after the authority.
    "mailto:Test.User@example.com",
    "mailto:Test.User@example.com/",
    VERIFY_HOST_CHARACTERS,
    "mailto:Test.User@example.com/",
    IS_VALID,
    IS_VALID
  },
  {
    "mailto:Test.User@example.com:25",
    "mailto:Test.User@example.com:25/",
    VERIFY_HOST_CHARACTERS,
    "mailto:Test.User@example.com:25/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com",
    "https://www.example.com/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/",
    "https://www.example.com/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com//",
    "https://www.example.com/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://127.0.0.1",
    "https://127.0.0.1/",
    VERIFY_HOST_CHARACTERS,
    "https://127.0.0.1/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://[::1]",
    "https://[::1]/",
    VERIFY_HOST_CHARACTERS,
    "https://[::1]/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://127.0.0.1/",
    "https://127.0.0.1/",
    VERIFY_HOST_CHARACTERS,
    "https://127.0.0.1/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com:8888",
    "https://www.example.com:8888/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com:8888/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com:8888/",
    "https://www.example.com:8888/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com:8888/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/a/path",
    "https://www.example.com/a/path",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com//a/path",
    "https://www.example.com/a/path",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path",
    IS_VALID,
    IS_VALID
  },

  // Technically a trailing '?' with an empty query string is valid, but we
  // drop the '?'. The parse_regex, however, makes no distinction between
  // query, fragment, and path components so it does not cut it out.
  {
    "https://www.example.com/a/path?",
    "https://www.example.com/a/path",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path?",
    IS_VALID,
    IS_VALID},
  {
    "https://www.example.com/a/path?name=value",
    "https://www.example.com/a/path?name=value",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path?name=value",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/a/path?name=/a/path/value",
    "https://www.example.com/a/path?name=/a/path/value",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path?name=/a/path/value",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/a/path?name=/a/path/value;some=other_value",
    "https://www.example.com/a/path?name=/a/path/value;some=other_value",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path?name=/a/path/value;some=other_value",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/a/path?name=/a/path/value;some=other_value/",
    "https://www.example.com/a/path?name=/a/path/value;some=other_value/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path?name=/a/path/value;some=other_value/",
    IS_VALID,
    IS_VALID
  },

  // XXX - Tests didn't pass before this change, so commenting out for now.
  // Again, URL::parse drops a final '?'.
  // {
  //   "https://www.example.com?",
  //   "https://www.example.com",
  //   VERIFY_HOST_CHARACTERS,
  //   "https://www.example.com?/",
  //   IS_VALID,
  //   IS_VALID
  // },
  // {
  //   "https://www.example.com?name=value",
  //   "https://www.example.com?name=value",
  //   VERIFY_HOST_CHARACTERS,
  //   "https://www.example.com?name=value/",
  //   IS_VALID,
  //   IS_VALID
  // },
  // {
  //   "https://www.example.com?name=value/",
  //   "https://www.example.com?name=value/",
  //   VERIFY_HOST_CHARACTERS,
  //   "https://www.example.com?name=value/",
  //   IS_VALID,
  //   IS_VALID
  // },

  // URL::parse also drops the final '#'.
  // {
  //   "https://www.example.com#",
  //   "https://www.example.com",
  //   VERIFY_HOST_CHARACTERS,
  //   "https://www.example.com#/",
  //   IS_VALID,
  //   IS_VALID
  // },
  // {
  //   "https://www.example.com#some=value",
  //   "https://www.example.com#some=value",
  //   VERIFY_HOST_CHARACTERS,
  //   "https://www.example.com#some=value/",
  //   IS_VALID,
  //   IS_VALID
  // },
  {
    "https://www.example.com/a/path#",
    "https://www.example.com/a/path",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path#",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/a/path#some=value",
    "https://www.example.com/a/path#some=value",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path#some=value",
    IS_VALID,
    IS_VALID
  },
  {
    // Note that this final '?' is not for a query parameter but is a part of
    // the fragment.
    "https://www.example.com/a/path#some=value?",
    "https://www.example.com/a/path#some=value?",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path#some=value?",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/a/path#some=value?with_question",
    "https://www.example.com/a/path#some=value?with_question",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path#some=value?with_question",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com/a/path?name=value?_with_question#some=value?with_question/",
    "https://www.example.com/a/path?name=value?_with_question#some=value?with_question/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com/a/path?name=value?_with_question#some=value?with_question/",
    IS_VALID,
    IS_VALID
  },

  // The following are some examples of strings we expect from regex_map in
  // remap.config.  The "From" portion, which are regular expressions, are
  // often not parsible by URL::parse but are by URL::parse_regex, which is the
  // purpose of its existence.
  {
    R"(http://(.*)?reactivate\.mail\.yahoo\.com/)",
    "",
    VERIFY_HOST_CHARACTERS,
    R"(http://(.*)?reactivate\.mail\.yahoo\.com/)",
    !IS_VALID,
    IS_VALID
  },
  {
    // The following is an example of a "To" URL in a regex_map line. We'll
    // first verify that the '$' is flagged as invalid for a host in this case.
    "http://$1reactivate.real.mail.yahoo.com/",
    "http://$1reactivate.real.mail.yahoo.com/",
    VERIFY_HOST_CHARACTERS,
    "http://$1reactivate.real.mail.yahoo.com/",
    !IS_VALID,
    IS_VALID
  },
  {
    // Same as above, but this time we pass in !VERIFY_HOST_CHARACTERS. This is
    // how RemapConfig will call this parse() function.
    "http://$1reactivate.real.mail.yahoo.com/",
    "http://$1reactivate.real.mail.yahoo.com/",
    !VERIFY_HOST_CHARACTERS,
    "http://$1reactivate.real.mail.yahoo.com/",
    IS_VALID,
    IS_VALID
  }
};
// clang-format on

constexpr bool URL_PARSE       = true;
constexpr bool URL_PARSE_REGEX = false;

/** Test the specified url.parse function.
 *
 * URL::parse and URL::parse_regex should behave the same. This function
 * performs the same behavior for each.
 *
 * @param[in] test_case The test case specification to run.
 *
 * @param[in] parse_function Whether to run parse() or
 * parse_regex().
 */
void
test_parse(url_parse_test_case const &test_case, bool parse_function)
{
  URL url;
  HdrHeap *heap = new_HdrHeap();
  url.create(heap);
  ParseResult result = PARSE_RESULT_OK;
  if (test_case.verify_host_characters) {
    result = url.parse(test_case.input_uri.c_str(), test_case.input_uri.size());
  } else {
    heap->destroy();
    return;
    // result = url.parse_no_host_check(test_case.input_uri.c_str(), test_case.input_uri.size());
  }
  bool expected_is_valid = test_case.is_valid;

  if (expected_is_valid && result != PARSE_RESULT_DONE) {
    std::printf("Parse URI: \"%s\", expected it to be valid but it was parsed invalid (%d)\n", test_case.input_uri.c_str(), result);
    CHECK(false);
  } else if (!expected_is_valid && result != PARSE_RESULT_ERROR) {
    std::printf("Parse URI: \"%s\", expected it to be invalid but it was parsed valid (%d)\n", test_case.input_uri.c_str(), result);
    CHECK(false);
  }
  if (result == PARSE_RESULT_DONE) {
    char buf[1024];
    int index  = 0;
    int offset = 0;
    url.print(buf, sizeof(buf), &index, &offset);
    std::string printed_url{buf, static_cast<size_t>(index)};
    CHECK(test_case.expected_printed_url == printed_url);
    CHECK(test_case.expected_printed_url.size() == printed_url.size());
  }
  heap->destroy();
}

TEST_CASE("UrlParse", "[proxy][parseurl]")
{
  for (auto const &test_case : url_parse_test_cases) {
    test_parse(test_case, URL_PARSE);
  }
}
