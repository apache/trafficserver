/** @file

  Unit tests for ControlMatcher.

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

#include "proxy/CacheControl.h"
#include "proxy/ControlMatcher.h"
#include "tscore/MatcherUtils.h"
#include "tscore/ink_memory.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace
{
class TestRequestData : public HttpRequestData
{
public:
  explicit TestRequestData(std::string_view url) : _url(url) {}

  char *
  get_string() override
  {
    return ats_strndup(_url.data(), _url.size());
  }

private:
  std::string_view _url;
};

void
parse_line(char *text, matcher_line &line, int line_number)
{
  REQUIRE(parseConfigLine(text, &line, &http_dest_tags) == nullptr);
  line.line_num = line_number;
}
} // namespace

TEST_CASE("UrlMatcher inserts and matches exact URLs", "[ControlMatcher]")
{
  UrlMatcher<CacheControlRecord, CacheControlResult> matcher{"CacheControl", "cache.config"};
  char                                               config[] = "url=http://example.com/exact action=never-cache";
  matcher_line                                       line;

  matcher.AllocateSpace(1);
  parse_line(config, line, 1);

  Result result = matcher.NewEntry(&line);

  REQUIRE_FALSE(result.failed());
  REQUIRE(matcher.num_el == 1);

  TestRequestData    exact_request{"http://example.com/exact"};
  CacheControlResult exact_result;

  matcher.Match(&exact_request, &exact_result);
  CHECK(exact_result.never_cache);

  TestRequestData    other_request{"http://example.com/other"};
  CacheControlResult other_result;

  matcher.Match(&other_request, &other_result);
  CHECK_FALSE(other_result.never_cache);
}

TEST_CASE("UrlMatcher does not insert invalid records", "[ControlMatcher]")
{
  UrlMatcher<CacheControlRecord, CacheControlResult> matcher{"CacheControl", "cache.config"};
  char                                               config[] = "url=http://example.com/exact action=invalid";
  matcher_line                                       line;

  matcher.AllocateSpace(1);
  parse_line(config, line, 1);

  Result result = matcher.NewEntry(&line);

  CHECK(result.failed());
  CHECK(matcher.num_el == 0);
}

TEST_CASE("UrlMatcher rejects duplicate URLs", "[ControlMatcher]")
{
  UrlMatcher<CacheControlRecord, CacheControlResult> matcher{"CacheControl", "cache.config"};
  char                                               first_config[]  = "url=http://example.com/exact action=never-cache";
  char                                               second_config[] = "url=http://example.com/exact action=standard-cache";
  matcher_line                                       first_line;
  matcher_line                                       second_line;

  matcher.AllocateSpace(2);
  parse_line(first_config, first_line, 1);
  parse_line(second_config, second_line, 2);

  Result first_result  = matcher.NewEntry(&first_line);
  Result second_result = matcher.NewEntry(&second_line);

  CHECK_FALSE(first_result.failed());
  CHECK(second_result.failed());
  CHECK(matcher.num_el == 1);
}
