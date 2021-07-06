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

TEST_CASE("ValidateURL", "[proxy][validurl]")
{
  static const struct {
    const char *const text;
    bool valid;
  } http_validate_hdr_field_test_case[] = {{"yahoo", true},
                                           {"yahoo.com", true},
                                           {"yahoo.wow.com", true},
                                           {"yahoo.wow.much.amaze.com", true},
                                           {"209.131.52.50", true},
                                           {"192.168.0.1", true},
                                           {"localhost", true},
                                           {"3ffe:1900:4545:3:200:f8ff:fe21:67cf", true},
                                           {"fe80:0:0:0:200:f8ff:fe21:67cf", true},
                                           {"fe80::200:f8ff:fe21:67cf", true},
                                           {"<svg onload=alert(1)>", false}, // Sample host header XSS attack
                                           {"jlads;f8-9349*(D&F*D(234jD*(FSD*(VKLJ#(*$@()#$)))))", false},
                                           {"\"\t\n", false},
                                           {"!@#$%^ &*(*&^%$#@#$%^&*(*&^%$#))", false},
                                           {":):(:O!!!!!!", false}};
  for (auto i : http_validate_hdr_field_test_case) {
    const char *const txt = i.text;
    if (validate_host_name({txt}) != i.valid) {
      std::printf("Validation of FQDN (host) header: \"%s\", expected %s, but not\n", txt, (i.valid ? "true" : "false"));
      CHECK(false);
    }
  }
}

namespace UrlImpl
{
bool url_is_strictly_compliant(const char *start, const char *end);
}
using namespace UrlImpl;

TEST_CASE("ParseRulesStrictURI", "[proxy][parseuri]")
{
  const struct {
    const char *const uri;
    bool valid;
  } http_strict_uri_parsing_test_case[] = {{"//index.html", true},
                                           {"/home", true},
                                           {"/path/data?key=value#id", true},
                                           {"/ABCDEFGHIJKLMNOPQRSTUVWXYZ", true},
                                           {"/abcdefghijklmnopqrstuvwxyz", true},
                                           {"/0123456789", true},
                                           {":/?#[]@", true},
                                           {"!$&'()*+,;=", true},
                                           {"-._~", true},
                                           {"%", true},
                                           {"\n", false},
                                           {"\"", false},
                                           {"<", false},
                                           {">", false},
                                           {"\\", false},
                                           {"^", false},
                                           {"`", false},
                                           {"{", false},
                                           {"|", false},
                                           {"}", false},
                                           {"é", false}};

  for (auto i : http_strict_uri_parsing_test_case) {
    const char *const uri = i.uri;
    if (url_is_strictly_compliant(uri, uri + strlen(uri)) != i.valid) {
      std::printf("Strictly parse URI: \"%s\", expected %s, but not\n", uri, (i.valid ? "true" : "false"));
      CHECK(false);
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
    "/index.html",
    "/index.html",
    VERIFY_HOST_CHARACTERS,
    "/index.html",
    IS_VALID,
    IS_VALID
  },
  {
    "//index.html",
    "//index.html",
    VERIFY_HOST_CHARACTERS,
    "//index.html",
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
    "https:////",
    VERIFY_HOST_CHARACTERS,
    "https:////",
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
    "https://www.example.com//",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com//",
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
    "https://www.example.com//a/path",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com//a/path",
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

  // Again, URL::parse drops a final '?'.
  {
    "https://www.example.com?",
    "https://www.example.com",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com?/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com?name=value",
    "https://www.example.com?name=value",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com?name=value/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com?name=value/",
    "https://www.example.com?name=value/",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com?name=value/",
    IS_VALID,
    IS_VALID
  },

  // URL::parse also drops the final '#'.
  {
    "https://www.example.com#",
    "https://www.example.com",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com#/",
    IS_VALID,
    IS_VALID
  },
  {
    "https://www.example.com#some=value",
    "https://www.example.com#some=value",
    VERIFY_HOST_CHARACTERS,
    "https://www.example.com#some=value/",
    IS_VALID,
    IS_VALID},
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
  if (parse_function == URL_PARSE) {
    if (test_case.verify_host_characters) {
      result = url.parse(test_case.input_uri);
    } else {
      result = url.parse_no_host_check(test_case.input_uri);
    }
  } else if (parse_function == URL_PARSE_REGEX) {
    result = url.parse_regex(test_case.input_uri);
  }
  bool expected_is_valid = test_case.is_valid;
  if (parse_function == URL_PARSE_REGEX) {
    expected_is_valid = test_case.is_valid_regex;
  }
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
    if (parse_function == URL_PARSE) {
      CHECK(test_case.expected_printed_url == printed_url);
      CHECK(test_case.expected_printed_url.size() == printed_url.size());
    } else if (parse_function == URL_PARSE_REGEX) {
      CHECK(test_case.expected_printed_url_regex == printed_url);
      CHECK(test_case.expected_printed_url_regex.size() == printed_url.size());
    }
  }
  heap->destroy();
}

TEST_CASE("UrlParse", "[proxy][parseurl]")
{
  for (auto const &test_case : url_parse_test_cases) {
    test_parse(test_case, URL_PARSE);
    test_parse(test_case, URL_PARSE_REGEX);
  }
}
