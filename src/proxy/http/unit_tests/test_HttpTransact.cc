/** @file

  Unit Tests for HttpTransact

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
#include <cstdio>

using namespace std::string_view_literals;

#include "tscore/Diags.h"
#include "tsutil/PostScript.h"

#include "proxy/http/HttpTransact.h"
#include "records/RecordsConfig.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("HttpTransact", "[http]")
{
  url_init();
  mime_init();
  http_init();

  SECTION("HttpTransact::merge_response_header_with_cached_header")
  {
    SECTION("Basic")
    {
      HTTPHdr        hdr1;
      HTTPHdr        hdr2;
      ts::PostScript hdr1_defer([&]() -> void { hdr1.destroy(); });
      ts::PostScript hdr2_defer([&]() -> void { hdr2.destroy(); });

      MIMEField *field;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {
        {"DDD", "444"},
        {"EEE", "555"},
        {"FFF", "666"}
      };

      hdr1.create(HTTPType::RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name);
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTPType::RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name);
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA"sv);
      REQUIRE(field != nullptr);
      auto str{field->value_get()};
      CHECK(str == "111"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "222"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "333"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "444"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("EEE"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "555"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("FFF"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "666"sv);
      CHECK(field->has_dups() == false);
    }

    SECTION("Have common headers")
    {
      HTTPHdr        hdr1;
      HTTPHdr        hdr2;
      ts::PostScript hdr1_defer([&]() -> void { hdr1.destroy(); });
      ts::PostScript hdr2_defer([&]() -> void { hdr2.destroy(); });

      MIMEField *field;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {
        {"DDD", "444"},
        {"BBB", "555"},
        {"FFF", "666"}
      };

      hdr1.create(HTTPType::RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name);
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTPType::RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name);
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 5);

      field = hdr1.field_find("AAA"sv);
      REQUIRE(field != nullptr);
      auto str{field->value_get()};
      CHECK(str == "111"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "555"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "333"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "444"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("FFF"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "666"sv);
      CHECK(field->has_dups() == false);
    }

    SECTION("Have dup headers")
    {
      HTTPHdr        hdr1;
      HTTPHdr        hdr2;
      ts::PostScript hdr1_defer([&]() -> void { hdr1.destroy(); });
      ts::PostScript hdr2_defer([&]() -> void { hdr2.destroy(); });

      MIMEField *field;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {
        {"DDD", "444"},
        {"EEE", "555"},
        {"EEE", "666"}
      };

      hdr1.create(HTTPType::RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name);
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTPType::RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name);
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA"sv);
      REQUIRE(field != nullptr);
      auto str{field->value_get()};
      CHECK(str == "111"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "222"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "333"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "444"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("EEE"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "555"sv);
      CHECK(field->has_dups() == true);
    }

    SECTION("Have dup headers 2")
    {
      HTTPHdr        hdr1;
      HTTPHdr        hdr2;
      ts::PostScript hdr1_defer([&]() -> void { hdr1.destroy(); });
      ts::PostScript hdr2_defer([&]() -> void { hdr2.destroy(); });

      MIMEField *field;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {
        {"DDD", "444"},
        {"DDD", "555"},
        {"FFF", "666"}
      };

      hdr1.create(HTTPType::RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name);
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTPType::RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name);
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA"sv);
      REQUIRE(field != nullptr);
      auto str{field->value_get()};
      CHECK(str == "111"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "222"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "333"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "444"sv);
      CHECK(field->has_dups() == true);

      field = hdr1.field_find("FFF"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "666"sv);
      CHECK(field->has_dups() == false);
    }

    SECTION("Have common and dup headers")
    {
      HTTPHdr        hdr1;
      HTTPHdr        hdr2;
      ts::PostScript hdr1_defer([&]() -> void { hdr1.destroy(); });
      ts::PostScript hdr2_defer([&]() -> void { hdr2.destroy(); });

      MIMEField *field;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
        {"DDD", "444"},
      };
      struct header input2[] = {
        {"AAA", "555"},
        {"BBB", "666"},
        {"BBB", "777"},
        {"CCC", "888"},
        {"EEE", "999"},
      };

      hdr1.create(HTTPType::RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name);
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTPType::RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name);
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA"sv);
      REQUIRE(field != nullptr);
      auto str{field->value_get()};
      CHECK(str == "555"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "666"sv);
      CHECK(field->has_dups() == true);

      ///////////// Dup //////////////////////////
      field = field->m_next_dup;
      str   = field->value_get();
      CHECK(str == "777"sv);
      CHECK(field->has_dups() == false);
      ///////////////////////////////////////

      field = hdr1.field_find("CCC"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "888"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "444"sv);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("EEE"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "999"sv);
      CHECK(field->has_dups() == false);
    }
    SECTION("Response has superset")
    {
      HTTPHdr        cached_headers;
      HTTPHdr        response_headers;
      ts::PostScript cached_headers_defer([&]() -> void { cached_headers.destroy(); });
      ts::PostScript response_headers_defer([&]() -> void { response_headers.destroy(); });

      MIMEField *field;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header cached[] = {
        {"Foo",   "111"},
        {"Fizz",  "555"},
        {"Bar",   "333"},
        {"Bop",   "666"},
        {"Bar",   "222"},
        {"X-Foo", "aaa"},
        {"Eat",   "444"},
      };
      // Response headers in a 304 should, in theory, match the cached headers, but, what if they don't?
      // The response headers should still be merged into the cached object properly given the existing logic.
      // In the following, the ordering is different from the cached headers, the Bar headers are missing, and two duplicate Zip
      // headers are not in the cached object.
      struct header response[] = {
        {"X-Foo", "aaa"},
        {"Zip",   "888"},
        {"Zip",   "999"},
        {"Eat",   "444"},
        {"Foo",   "111"},
        {"Fizz",  "555"},
        {"Bop",   "666"},
      };

      cached_headers.create(HTTPType::RESPONSE);
      for (auto &&entry : cached) {
        field = cached_headers.field_create(entry.name);
        cached_headers.field_attach(field);
        cached_headers.field_value_set(field, entry.value.data(), entry.value.length());
      }

      response_headers.create(HTTPType::RESPONSE);
      for (auto &&entry : response) {
        field = response_headers.field_create(entry.name);
        response_headers.field_attach(field);
        response_headers.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&cached_headers, &response_headers);

      CHECK(cached_headers.fields_count() == 9);
      CHECK(response_headers.fields_count() == 7);

      field = cached_headers.field_find("Foo"sv);
      REQUIRE(field != nullptr);
      auto str{field->value_get()};
      CHECK(str == "111"sv);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Fizz"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "555"sv);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Bop"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "666"sv);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("X-Foo"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "aaa"sv);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Eat"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "444"sv);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Bar"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "333"sv);
      CHECK(field->has_dups() == true);

      ///////////// Dup //////////////////////////
      field = field->m_next_dup;
      str   = field->value_get();
      CHECK(str == "222"sv);
      CHECK(field->has_dups() == false);
      ///////////////////////////////////////

      field = cached_headers.field_find("Zip"sv);
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "888"sv);
      CHECK(field->has_dups() == true);

      ///////////// Dup //////////////////////////
      REQUIRE(field->m_next_dup != nullptr);
      field = field->m_next_dup;
      REQUIRE(field != nullptr);
      str = field->value_get();
      CHECK(str == "999"sv);
      CHECK(field->has_dups() == false);
      ///////////////////////////////////////
    }

    // Regression: repeated 304 revalidation must not grow the cached header
    // without bound (see merge_response_header_with_cached_header). The
    // yardstick is HdrHeap::marshal_length() -- the size that gates the cache
    // write -- NOT fields_count(): dead MIMEField slots grow the heap even
    // while the live field count stays constant.
    SECTION("Repeated revalidation does not grow the cached header heap")
    {
      struct header {
        std::string_view name;
        std::string_view value;
      };

      auto build = [](HTTPHdr &h, const std::vector<header> &fields) {
        h.create(HTTPType::RESPONSE);
        for (auto &&e : fields) {
          MIMEField *f = h.field_create(e.name);
          h.field_attach(f);
          h.field_value_set(f, e.value);
        }
      };

      // Merge `response` into a copy of `cached0` `iterations` times and return
      // the growth in the cached header's marshalled heap size. A correct merge
      // is idempotent, so the growth must be bounded (independent of iterations).
      auto merge_growth = [&](const std::vector<header> &cached0, const std::vector<header> &response, int iterations) -> int {
        HTTPHdr        cached;
        HTTPHdr        resp;
        ts::PostScript cached_defer([&]() -> void { cached.destroy(); });
        ts::PostScript resp_defer([&]() -> void { resp.destroy(); });
        build(cached, cached0);
        build(resp, response);
        // One warm-up merge so any one-time restructuring settles before measuring.
        HttpTransact::merge_response_header_with_cached_header(&cached, &resp);
        int const before = cached.m_heap->marshal_length();
        for (int i = 0; i < iterations; ++i) {
          HttpTransact::merge_response_header_with_cached_header(&cached, &resp);
        }
        return cached.m_heap->marshal_length() - before;
      };

      // No duplicated fields: pure in-place replacement, bounded.
      CHECK(merge_growth(
              {
                {"Date",    "d0"},
                {"Expires", "e0"},
                {"Etag",    "t0"}
      },
              {{"Date", "d1"}, {"Expires", "e1"}, {"Etag", "t1"}}, 4000) <= 2048);

      // A duplicated field (e.g. Cache-Control: max-age=60, public) ahead of
      // single-valued fields must not force those single-valued fields down an
      // appending path. Before the fix the sticky "dups_seen" flag did exactly
      // that and this grew ~132 bytes per merge (megabytes over a day).
      CHECK(merge_growth(
              {
                {"Cache-Control", "max-age=60"},
                {"Cache-Control", "public"    },
                {"Expires",       "e0"        },
                {"Etag",          "t0"        }
      },
              {{"Cache-Control", "max-age=60"}, {"Cache-Control", "public"}, {"Expires", "e0"}, {"Etag", "t0"}}, 4000) <= 2048);

      // Response carries a duplicated field the cached copy does not yet have.
      CHECK(merge_growth(
              {
                {"Date",    "d0"},
                {"Expires", "e0"},
                {"Etag",    "t0"}
      },
              {{"Date", "d0"}, {"Vary", "A"}, {"Vary", "B"}, {"Expires", "e0"}, {"Etag", "t0"}}, 4000) <= 2048);

      // A response value that changes length every merge (as Expires does on a
      // real 304) must still be bounded: value_set frees the old string (self-
      // coalesced) and reuses the slot.
      {
        HTTPHdr        cached;
        HTTPHdr        resp;
        ts::PostScript cached_defer([&]() -> void { cached.destroy(); });
        ts::PostScript resp_defer([&]() -> void { resp.destroy(); });
        build(cached, {
                        {"Server",        "ATS"       },
                        {"Cache-Control", "max-age=60"},
                        {"Date",          "d0"        },
                        {"Expires",       "e0"        },
                        {"Etag",          "t0"        }
        });
        resp.create(HTTPType::RESPONSE);
        MIMEField *rf = resp.field_create("Expires"sv);
        resp.field_attach(rf);
        resp.field_value_set(rf, "e0"sv);
        HttpTransact::merge_response_header_with_cached_header(&cached, &resp);
        int const before = cached.m_heap->marshal_length();
        char      buf[64];
        for (int i = 0; i < 2000; ++i) {
          int n = std::snprintf(buf, sizeof(buf), "Sat, 18 Jul 2026 05:%02d:%02d GMT-%d", i % 60, (i * 7) % 60, i % 1000);
          resp.field_value_set(rf, std::string_view(buf, n));
          HttpTransact::merge_response_header_with_cached_header(&cached, &resp);
        }
        MIMEField *exp = cached.field_find("Expires"sv);
        REQUIRE(exp != nullptr);
        CHECK(exp->has_dups() == false);
        CHECK(cached.m_heap->marshal_length() - before <= 4096);
      }

      // Correctness: merging a response that DROPS one of two duplicate values
      // must leave the cached header with exactly the response's values.
      {
        HTTPHdr        cached;
        HTTPHdr        resp;
        ts::PostScript cached_defer([&]() -> void { cached.destroy(); });
        ts::PostScript resp_defer([&]() -> void { resp.destroy(); });
        build(cached, {
                        {"Vary", "A" },
                        {"Vary", "B" },
                        {"Vary", "C" },
                        {"Etag", "t0"}
        });
        build(resp, {
                      {"Vary", "X" },
                      {"Vary", "Y" },
                      {"Etag", "t1"}
        });
        HttpTransact::merge_response_header_with_cached_header(&cached, &resp);
        int              count = 0;
        std::string_view v0, v1;
        for (MIMEField *d = cached.field_find("Vary"sv); d != nullptr; d = d->m_next_dup) {
          if (count == 0) {
            v0 = d->value_get();
          } else if (count == 1) {
            v1 = d->value_get();
          }
          ++count;
        }
        CHECK(count == 2); // surplus cached "C" removed
        CHECK(v0 == "X"sv);
        CHECK(v1 == "Y"sv);
        MIMEField *etag = cached.field_find("Etag"sv);
        REQUIRE(etag != nullptr);
        CHECK(etag->value_get() == "t1"sv);
        CHECK(etag->has_dups() == false);
      }

      // The production caller sequence: merge_and_update_headers_for_cache_update
      // deletes a caching header only when the 304 OMITS it, then merges. This
      // exercises the conditional-delete + the cooked WKS headers
      // (Cache-Control/Expires/Age) end to end, over many revalidations with a
      // changing Expires. The cached header heap and the cooked freshness values
      // must stay correct and bounded.
      {
        HTTPHdr        cached;
        ts::PostScript cached_defer([&]() -> void { cached.destroy(); });
        build(cached, {
                        {"Server",        "ATS"       },
                        {"Cache-Control", "max-age=60"},
                        {"Date",          "d0"        },
                        {"Expires",       "e0"        },
                        {"Etag",          "t0"        }
        });

        int settled = 0;
        for (int i = 0; i < 2000; ++i) {
          HTTPHdr        resp;
          ts::PostScript resp_defer([&]() -> void { resp.destroy(); });
          resp.create(HTTPType::RESPONSE);
          MIMEField *f = resp.field_create("Cache-Control"sv);
          resp.field_attach(f);
          resp.field_value_set(f, "max-age=60"sv);
          char buf[64];
          int  n = std::snprintf(buf, sizeof(buf), "Sat, 18 Jul 2026 05:%02d:%02d GMT", i % 60, (i * 7) % 60);
          f      = resp.field_create("Expires"sv);
          resp.field_attach(f);
          resp.field_value_set(f, std::string_view(buf, n));

          // Mirror merge_and_update_headers_for_cache_update: delete a caching
          // header only if the response omits it, then merge.
          if (!resp.presence(MIME_PRESENCE_AGE)) {
            cached.field_delete(static_cast<std::string_view>(MIME_FIELD_AGE));
          }
          if (!resp.presence(MIME_PRESENCE_ETAG)) {
            cached.field_delete(static_cast<std::string_view>(MIME_FIELD_ETAG));
          }
          if (!resp.presence(MIME_PRESENCE_EXPIRES)) {
            cached.field_delete(static_cast<std::string_view>(MIME_FIELD_EXPIRES));
          }
          HttpTransact::merge_response_header_with_cached_header(&cached, &resp);
          if (i == 0) {
            settled = cached.m_heap->marshal_length();
          }
        }
        // Etag was omitted by every 304, so it must have been dropped from cache.
        CHECK(cached.field_find("Etag"sv) == nullptr);
        // Expires stays single-valued and the cooked max-age is intact.
        MIMEField *exp = cached.field_find("Expires"sv);
        REQUIRE(exp != nullptr);
        CHECK(exp->has_dups() == false);
        CHECK(cached.get_cooked_cc_max_age() == 60);
        // Heap stays bounded across 2000 revalidations.
        CHECK(cached.m_heap->marshal_length() - settled <= 4096);
      }
    }
  }
}
