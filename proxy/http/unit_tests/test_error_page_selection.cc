/** @file

  Catch-based tests of error page selection.

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

#include "catch.hpp"
#include "HttpBodyFactory.h"
#include <array>

TEST_CASE("error page selection test", "[http]")
{
  struct Sets {
    const char *set_name;
    const char *content_language;
    const char *content_charset;
  };

  std::array<Sets, 10> sets = {{{"default", "en", "iso-8859-1"},
                                {"en-cockney", "en-cockney", "iso-8859-1"},
                                {"en0", "en", "iso-8859-1"},
                                {"en-us", "en-us", "us-ascii"},
                                {"en1", "en", "unicode"},
                                {"en-cockney-slang", "en-cockney-slang", "iso-8859-1"},
                                {"ko0", "ko", "iso-8859-1"},
                                {"ko1", "ko", "iso-2022-kr"},
                                {"jp", "jp", "shift-jis"},
                                {"es", "es", "unicode"}}};

  struct Tests {
    const char *accept_language;
    const char *accept_charset;
    const char *expected_set;
    float expected_Q;
    int expected_La;
    int expected_I;
  };

  std::array<Tests, 26> tests = {{
    {nullptr, nullptr, "default", 1, 0, INT_MAX},
    {"en", "iso-8859-1", "en0", 1, 2, 1},
    {"en", "unicode", "en1", 1, 2, 1},
    {"ko", "iso-8859-1", "ko0", 1, 2, 1},
    {"ko", "iso-2022-kr", "ko1", 1, 2, 1},
    {"en-us", nullptr, "en-us", 1, 5, 1},
    {"en-US", nullptr, "en-us", 1, 5, 1},
    {"jp,es", nullptr, "jp", 1, 2, 1},
    {"es,jp", nullptr, "es", 1, 2, 1},
    {"jp;q=0.7,es", nullptr, "es", 1, 2, 2},
    {"jp;q=.7,es", nullptr, "es", 1, 2, 2},
    {"jp;q=.7,es;q=.7", nullptr, "jp", 0.7, 2, 1},
    {"jp;q=.7,es;q=.701", nullptr, "es", 0.701, 2, 2},
    {"jp;q=.7  ,  es;q=.701", nullptr, "es", 0.701, 2, 2},
    {"jp  ;  q=.7  ,  es  ;  ;  ;  ; q=.701", nullptr, "es", 0.701, 2, 2},
    {"jp,es;q=.7", nullptr, "jp", 1, 2, 1},
    {"jp;q=1,es;q=.7", nullptr, "jp", 1, 2, 1},
    {"jp;;;q=1,es;q=.7", nullptr, "jp", 1, 2, 1},
    {"jp;;;q=1,,,,es;q=.7", nullptr, "jp", 1, 2, 1},
    {"jp;;;q=.7,,,,es;q=.7", nullptr, "jp", 0.7, 2, 1},
    {"jp;;;q=.699,,,,es;q=.7", nullptr, "es", 0.7, 2, 5},
    {"jp;q=0,es;q=1", nullptr, "es", 1, 2, 2},
    {"jp;q=0, es;q=1", nullptr, "es", 1, 2, 2},
    {"jp;q=0,es;q=.5", nullptr, "es", 0.5, 2, 2},
    {"jp;q=0, es;q=.5", nullptr, "es", 0.5, 2, 2},
    {"jp;q=000000000.00000000000000000000,es;q=1.0000000000000000000", nullptr, "es", 1, 2, 2},
  }};

  // (1) build fake hash table of sets
  std::unique_ptr<HttpBodyFactory::BodySetTable> table_of_sets;
  table_of_sets.reset(new HttpBodyFactory::BodySetTable);
  for (const auto &set : sets) {
    HttpBodySetRawData *body_set = new HttpBodySetRawData;
    body_set->magic              = 0;
    body_set->set_name           = strdup(set.set_name);
    body_set->content_language   = strdup(set.content_language);
    body_set->content_charset    = strdup(set.content_charset);
    body_set->table_of_pages.reset(new HttpBodySetRawData::TemplateTable);
    REQUIRE(table_of_sets->find(body_set->set_name) == table_of_sets->end());
    table_of_sets->emplace(body_set->set_name, body_set);
  }

  // (2) for each test, parse accept headers into lists, and test matching
  int count = 0;
  for (const auto &test : tests) {
    float Q_best;
    int La_best, Lc_best, I_best;
    const char *set_best;
    StrList accept_language_list;
    StrList accept_charset_list;
    HttpCompat::parse_comma_list(&accept_language_list, test.accept_language);
    HttpCompat::parse_comma_list(&accept_charset_list, test.accept_charset);
    printf("         test #%d: (Accept-Language='%s', Accept-Charset='%s')\n", ++count,
           (test.accept_language ? test.accept_language : "<null>"), (test.accept_charset ? test.accept_charset : "<null>"));
    set_best = HttpBodyFactory::determine_set_by_language(table_of_sets, &(accept_language_list), &(accept_charset_list), &Q_best,
                                                          &La_best, &Lc_best, &I_best);
    REQUIRE(strcmp(set_best, test.expected_set) == 0);
    REQUIRE(Q_best == test.expected_Q);
    REQUIRE(La_best == test.expected_La);
    REQUIRE(I_best == test.expected_I);
  }

  for (const auto &it : *table_of_sets.get()) {
    ats_free(it.second->set_name);
    ats_free(it.second->content_language);
    ats_free(it.second->content_charset);
    it.second->table_of_pages.reset(nullptr);
    delete it.second;
  }
  table_of_sets.reset(nullptr);
}
