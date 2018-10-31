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

TEST_CASE("error page selection test", "[http]")
{
  static struct {
    const char *set_name;
    const char *content_language;
    const char *content_charset;
  } sets[] = {{"default", "en", "iso-8859-1"}, {"en-cockney", "en-cockney", "iso-8859-1"},
              {"en", "en", "iso-8859-1"},      {"en-us", "en-us", "us-ascii"},
              {"en", "en", "unicode"},         {"en-cockney-slang", "en-cockney-slang", "iso-8859-1"},
              {"ko", "ko", "iso-8859-1"},      {"ko", "ko", "iso-2022-kr"},
              {"jp", "jp", "shift-jis"}};
  static struct {
    const char *accept_language;
    const char *accept_charset;
    const char *expected_set;
    float expected_Q;
    int expected_La;
    int expected_I;
  } tests[] = {
    {nullptr, nullptr, "default", 1, 0, INT_MAX},
    {"en", nullptr, "en", 1, 2, 1},
    {"ko", nullptr, "ko", 1, 2, 1},
    {"en-us", nullptr, "en-us", 1, 5, 1},
    {"en-US", nullptr, "en-us", 1, 5, 1},
    {"en,ko", nullptr, "en", 1, 2, 1},
    {"ko,en", nullptr, "ko", 1, 2, 1},
    {"en;q=0.7,ko", nullptr, "ko", 1, 2, 2},
    {"en;q=.7,ko", nullptr, "ko", 1, 2, 2},
    {"en;q=.7,ko;q=.7", nullptr, "en", 0.7, 2, 1},
    {"en;q=.7,ko;q=.701", nullptr, "ko", 0.701, 2, 2},
    {"en;q=.7  ,  ko;q=.701", nullptr, "ko", 0.701, 2, 2},
    {"en  ;  q=.7  ,  ko  ;  ;  ;  ; q=.701", nullptr, "ko", 0.701, 2, 2},
    {"en,ko;q=.7", nullptr, "en", 1, 2, 1},
    {"en;q=1,ko;q=.7", nullptr, "en", 1, 2, 1},
    {"en;;;q=1,ko;q=.7", nullptr, "en", 1, 2, 1},
    {"en;;;q=1,,,,ko;q=.7", nullptr, "en", 1, 2, 1},
    {"en;;;q=.7,,,,ko;q=.7", nullptr, "en", 0.7, 2, 1},
    {"en;;;q=.699,,,,ko;q=.7", nullptr, "ko", 0.7, 2, 5},
    {"en;q=0,ko;q=1", nullptr, "ko", 1, 2, 2},
    {"en;q=0, ko;q=1", nullptr, "ko", 1, 2, 2},
    {"en;q=0,ko;q=.5", nullptr, "ko", 0.5, 2, 2},
    {"en;q=0, ko;q=.5", nullptr, "ko", 0.5, 2, 2},
    {"en;q=000000000.00000000000000000000,ko;q=1.0000000000000000000", nullptr, "ko", 1, 2, 2},
  };

  int i;
  int nsets  = sizeof(sets) / sizeof(sets[0]);
  int ntests = sizeof(tests) / sizeof(tests[0]);
  // (1) build fake hash table of sets
  RawHashTable *table_of_sets = new RawHashTable(RawHashTable_KeyType_String);
  for (i = 0; i < nsets; i++) {
    HttpBodySetRawData *body_set;
    body_set                   = (HttpBodySetRawData *)ats_malloc(sizeof(HttpBodySetRawData));
    body_set->magic            = 0;
    body_set->set_name         = (char *)(sets[i].set_name);
    body_set->content_language = (char *)(sets[i].content_language);
    body_set->content_charset  = (char *)(sets[i].content_charset);
    body_set->table_of_pages   = (RawHashTable *)1; // hack --- can't be NULL
    table_of_sets->setValue((RawHashTable_Key)(body_set->set_name), (RawHashTable_Value)body_set);
  }
  // (2) for each test, parse accept headers into lists, and test matching
  for (i = 0; i < ntests; i++) {
    float Q_best;
    int La_best, Lc_best, I_best;
    const char *set_best;
    StrList accept_language_list;
    StrList accept_charset_list;
    HttpCompat::parse_comma_list(&accept_language_list, tests[i].accept_language);
    HttpCompat::parse_comma_list(&accept_charset_list, tests[i].accept_charset);
    printf("         test #%d: (Accept-Language='%s', Accept-Charset='%s')\n", i + 1,
           (tests[i].accept_language ? tests[i].accept_language : "<null>"),
           (tests[i].accept_charset ? tests[i].accept_charset : "<null>"));
    set_best = HttpBodyFactory::determine_set_by_language(table_of_sets, &(accept_language_list), &(accept_charset_list), &Q_best,
                                                          &La_best, &Lc_best, &I_best);
    REQUIRE(strcmp(set_best, tests[i].expected_set) == 0);
    REQUIRE(Q_best == tests[i].expected_Q);
    REQUIRE(La_best == tests[i].expected_La);
    REQUIRE(I_best == tests[i].expected_I);
  }
  delete table_of_sets;
}
