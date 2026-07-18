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
#include <chrono>
#include <algorithm>

using namespace std::string_view_literals;

#include "tscore/Diags.h"
#include "tsutil/PostScript.h"

#include "proxy/http/HttpTransact.h"
#include "proxy/http/HttpTransactHeaders.h"
#include "records/RecordsConfig.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// Faithful copy of merge_response_header_with_cached_header BEFORE the fix, so the
// old and new implementations can be timed head-to-head in one process (no
// cross-build variance). Kept only for the "[.merge-bench]" benchmark below.
void
merge_OLD(HTTPHdr *cached_header, HTTPHdr *response_header)
{
  MIMEField *new_field;
  bool       dups_seen = false;

  for (auto spot = response_header->begin(), limit = response_header->end(); spot != limit; ++spot) {
    MIMEField &field{*spot};
    auto       name{field.name_get()};

    if (HttpTransactHeaders::is_this_a_hop_by_hop_header(name.data())) {
      continue;
    }
    if (name.data() == MIME_FIELD_CONTENT_LENGTH.c_str() || name.data() == MIME_FIELD_TRANSFER_ENCODING.c_str()) {
      continue;
    }
    if (name.data() == MIME_FIELD_SET_COOKIE.c_str()) {
      continue;
    }
    if (name.data() == MIME_FIELD_CONTENT_TYPE.c_str()) {
      continue;
    }
    if (name.data() == MIME_FIELD_WARNING.c_str()) {
      continue;
    }
    if (field.m_next_dup) {
      if (dups_seen == false) {
        for (auto spot2 = spot; spot2 != limit; ++spot2) {
          MIMEField &field2{*spot2};
          auto       name2{field2.name_get()};
          if (name2.data() == MIME_FIELD_CONTENT_TYPE.c_str()) {
            continue;
          }
          cached_header->field_delete(name2);
        }
        dups_seen = true;
      }
    }

    auto value{field.value_get()};

    if (dups_seen == false) {
      cached_header->value_set(name, value);
    } else {
      new_field = cached_header->field_create(name);
      cached_header->field_attach(new_field);
      cached_header->field_value_set(new_field, value);
    }
  }

  HttpTransact::merge_warning_header(cached_header, response_header);
}
} // namespace

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

    // Regression: repeated 304 revalidation of the same object must not grow the
    // cached header without bound. In production a hot max-age=60 object is
    // revalidated on ~every request; each 304 re-merges the server response into
    // the cached header. If the merge grows the cached header's HdrHeap, its
    // marshalled size eventually crosses the aggregation fragment limit and the
    // cache write can never commit again (object frozen stale forever).
    //
    // The yardstick is HdrHeap::marshal_length() (the size that gates the cache
    // write), NOT fields_count(): a deleted-then-recreated field strands a dead
    // MIMEField slot that is not reclaimed until its whole block empties, so the
    // heap grows even while the live field count is constant.
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

// Hot-path microbenchmark: merge_response_header_with_cached_header runs on every
// 304 revalidation. Compares the new implementation against a faithful copy of the
// pre-fix one (merge_OLD), head-to-head in one process. Hidden by the '.' tag; run
// explicitly with:  test_http "[merge-bench]"
TEST_CASE("merge hot-path benchmark", "[.merge-bench]")
{
  url_init();
  mime_init();
  http_init();

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

  // A realistic stored 200 for a small, frequently revalidated object (the cached
  // object): a handful of headers including the caching triplet Cache-Control /
  // Etag / Expires.
  std::vector<header> const cached_tmpl = {
    {"Server",                    "ATS"                                },
    {"Date",                      "Sat, 18 Jul 2026 05:29:18 GMT"      },
    {"Content-Type",              "text/xml"                           },
    {"Content-Length",            "181"                                },
    {"Last-Modified",             "Tue, 04 Apr 2023 17:58:01 GMT"      },
    {"Accept-Ranges",             "bytes"                              },
    {"Cache-Control",             "max-age=60"                         },
    {"Content-Language",          "en-us"                              },
    {"Strict-Transport-Security", "max-age=31536000; includeSubdomains"},
    {"X-Frame-Options",           "SAMEORIGIN"                         },
    {"X-Content-Type-Options",    "nosniff"                            },
    {"X-XSS-Protection",          "1; mode=block"                      },
    {"Etag",                      "\"0123456789abcdef\""               },
    {"Expires",                   "Sat, 18 Jul 2026 05:30:18 GMT"      },
  };

  // A typical minimal 304 (no duplicated fields).
  std::vector<header> const resp_typical = {
    {"Server",        "ATS"                          },
    {"Date",          "Sat, 18 Jul 2026 05:30:18 GMT"},
    {"Cache-Control", "max-age=60"                   },
    {"Expires",       "Sat, 18 Jul 2026 05:31:18 GMT"},
  };

  // The pathological 304 that triggered the leak: a duplicated Cache-Control.
  std::vector<header> const resp_dup = {
    {"Server",        "ATS"                          },
    {"Date",          "Sat, 18 Jul 2026 05:30:18 GMT"},
    {"Cache-Control", "max-age=60"                   },
    {"Cache-Control", "public"                       },
    {"Expires",       "Sat, 18 Jul 2026 05:31:18 GMT"},
  };

  using MergeFn = void (*)(HTTPHdr *, HTTPHdr *);

  // Time pure merge cost: pre-build a pool of fresh cached copies (untimed), then
  // time only the merges. Each copy is merged exactly once, so this is fair for
  // both the idempotent new code and the leaking old code. Returns ns/merge.
  auto bench = [&](MergeFn fn, const std::vector<header> &resp_fields, int total, int pool_sz) -> double {
    HTTPHdr        resp;
    ts::PostScript resp_defer([&]() -> void { resp.destroy(); });
    build(resp, resp_fields);

    long long best_ns = -1;
    for (int rep = 0; rep < 5; ++rep) {
      long long rep_ns = 0;
      int       done   = 0;
      while (done < total) {
        int      k    = std::min(pool_sz, total - done);
        HTTPHdr *pool = new HTTPHdr[k];
        // Build fresh cached copies (untimed) so the timed region is pure merges.
        for (int i = 0; i < k; ++i) {
          pool[i].create(HTTPType::RESPONSE);
          for (auto &&e : cached_tmpl) {
            MIMEField *f = pool[i].field_create(e.name);
            pool[i].field_attach(f);
            pool[i].field_value_set(f, e.value);
          }
        }
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < k; ++i) {
          fn(&pool[i], &resp);
        }
        auto t1  = std::chrono::steady_clock::now();
        rep_ns  += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        for (int i = 0; i < k; ++i) {
          pool[i].destroy();
        }
        delete[] pool;
        done += k;
      }
      if (best_ns < 0 || rep_ns < best_ns) {
        best_ns = rep_ns;
      }
    }
    return static_cast<double>(best_ns) / total;
  };

  // Equivalence sanity: old and new must produce the same field set.
  {
    HTTPHdr        c_old, c_new, resp;
    ts::PostScript d1([&]() -> void { c_old.destroy(); });
    ts::PostScript d2([&]() -> void { c_new.destroy(); });
    ts::PostScript d3([&]() -> void { resp.destroy(); });
    build(c_old, cached_tmpl);
    build(c_new, cached_tmpl);
    build(resp, resp_dup);
    merge_OLD(&c_old, &resp);
    HttpTransact::merge_response_header_with_cached_header(&c_new, &resp);
    CHECK(c_old.fields_count() == c_new.fields_count());
  }

  int const N    = 300000;
  int const POOL = 2000;

  double old_typical = bench(&merge_OLD, resp_typical, N, POOL);
  double new_typical = bench(&HttpTransact::merge_response_header_with_cached_header, resp_typical, N, POOL);
  double old_dup     = bench(&merge_OLD, resp_dup, N, POOL);
  double new_dup     = bench(&HttpTransact::merge_response_header_with_cached_header, resp_dup, N, POOL);

  // Full revalidation unit = the caller's caching-header delete + the merge, i.e.
  // what merge_and_update_headers_for_cache_update does per 304. OLD deletes
  // Age/ETag/Expires unconditionally; NEW deletes only those the response omits,
  // then merges. This is the real per-revalidation hot-path cost.
  auto bench_unit = [&](bool is_new, const std::vector<header> &resp_fields, int total, int pool_sz) -> double {
    HTTPHdr        resp;
    ts::PostScript resp_defer([&]() -> void { resp.destroy(); });
    build(resp, resp_fields);
    bool const resp_has_age     = resp.presence(MIME_PRESENCE_AGE) != 0;
    bool const resp_has_etag    = resp.presence(MIME_PRESENCE_ETAG) != 0;
    bool const resp_has_expires = resp.presence(MIME_PRESENCE_EXPIRES) != 0;

    long long best_ns = -1;
    for (int rep = 0; rep < 5; ++rep) {
      long long rep_ns = 0;
      int       done   = 0;
      while (done < total) {
        int      k    = std::min(pool_sz, total - done);
        HTTPHdr *pool = new HTTPHdr[k];
        for (int i = 0; i < k; ++i) {
          pool[i].create(HTTPType::RESPONSE);
          for (auto &&e : cached_tmpl) {
            MIMEField *f = pool[i].field_create(e.name);
            pool[i].field_attach(f);
            pool[i].field_value_set(f, e.value);
          }
        }
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < k; ++i) {
          HTTPHdr *c = &pool[i];
          if (is_new) {
            if (!resp_has_age) {
              c->field_delete(static_cast<std::string_view>(MIME_FIELD_AGE));
            }
            if (!resp_has_etag) {
              c->field_delete(static_cast<std::string_view>(MIME_FIELD_ETAG));
            }
            if (!resp_has_expires) {
              c->field_delete(static_cast<std::string_view>(MIME_FIELD_EXPIRES));
            }
            HttpTransact::merge_response_header_with_cached_header(c, &resp);
          } else {
            c->field_delete(static_cast<std::string_view>(MIME_FIELD_AGE));
            c->field_delete(static_cast<std::string_view>(MIME_FIELD_ETAG));
            c->field_delete(static_cast<std::string_view>(MIME_FIELD_EXPIRES));
            merge_OLD(c, &resp);
          }
        }
        auto t1  = std::chrono::steady_clock::now();
        rep_ns  += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        for (int i = 0; i < k; ++i) {
          pool[i].destroy();
        }
        delete[] pool;
        done += k;
      }
      if (best_ns < 0 || rep_ns < best_ns) {
        best_ns = rep_ns;
      }
    }
    return static_cast<double>(best_ns) / total;
  };

  double old_unit_typical = bench_unit(false, resp_typical, N, POOL);
  double new_unit_typical = bench_unit(true, resp_typical, N, POOL);
  double old_unit_dup     = bench_unit(false, resp_dup, N, POOL);
  double new_unit_dup     = bench_unit(true, resp_dup, N, POOL);

  std::printf("\n=== merge hot-path benchmark (ns/op, best of 5, N=%d) ===\n", N);
  std::printf("merge only:\n");
  std::printf("  typical 304 (no dup):  OLD %7.1f   NEW %7.1f   (%+.1f%%)\n", old_typical, new_typical,
              100.0 * (new_typical - old_typical) / old_typical);
  std::printf("  dup 304 (Cache-Ctrl):  OLD %7.1f   NEW %7.1f   (%+.1f%%)\n", old_dup, new_dup,
              100.0 * (new_dup - old_dup) / old_dup);
  std::printf("full revalidation unit (caller delete + merge):\n");
  std::printf("  typical 304 (no dup):  OLD %7.1f   NEW %7.1f   (%+.1f%%)\n", old_unit_typical, new_unit_typical,
              100.0 * (new_unit_typical - old_unit_typical) / old_unit_typical);
  std::printf("  dup 304 (Cache-Ctrl):  OLD %7.1f   NEW %7.1f   (%+.1f%%)\n", old_unit_dup, new_unit_dup,
              100.0 * (new_unit_dup - old_unit_dup) / old_unit_dup);
  std::printf("  (negative %% = new is faster)\n\n");

  CHECK(new_typical > 0.0);
}
