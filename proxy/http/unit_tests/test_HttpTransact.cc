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
#include "tscore/Diags.h"
#include "HttpTransact.h"

#include "catch.hpp"

TEST_CASE("HttpTransact", "[http]")
{
  url_init();
  mime_init();
  http_init();

  SECTION("HttpTransact::merge_response_header_with_cached_header")
  {
    SECTION("Basic")
    {
      HTTPHdr hdr1;
      HTTPHdr hdr2;
      MIMEField *field;
      const char *str;
      int len;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {{"DDD", "444"}, {"EEE", "555"}, {"FFF", "666"}};

      hdr1.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name.data(), entry.name.length());
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name.data(), entry.name.length());
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "111", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "222", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "333", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "444", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("EEE", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "555", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("FFF", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "666", len) == 0);
      CHECK(field->has_dups() == false);
    }

    SECTION("Have comon headers")
    {
      HTTPHdr hdr1;
      HTTPHdr hdr2;
      MIMEField *field;
      const char *str;
      int len;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {{"DDD", "444"}, {"BBB", "555"}, {"FFF", "666"}};

      hdr1.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name.data(), entry.name.length());
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name.data(), entry.name.length());
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 5);

      field = hdr1.field_find("AAA", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "111", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "555", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "333", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "444", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("FFF", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "666", len) == 0);
      CHECK(field->has_dups() == false);
    }

    SECTION("Have dup headers")
    {
      HTTPHdr hdr1;
      HTTPHdr hdr2;
      MIMEField *field;
      const char *str;
      int len;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {{"DDD", "444"}, {"EEE", "555"}, {"EEE", "666"}};

      hdr1.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name.data(), entry.name.length());
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name.data(), entry.name.length());
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "111", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "222", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "333", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "444", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("EEE", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "555", len) == 0);
      CHECK(field->has_dups() == true);
    }

    SECTION("Have dup headers 2")
    {
      HTTPHdr hdr1;
      HTTPHdr hdr2;
      MIMEField *field;
      const char *str;
      int len;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header input1[] = {
        {"AAA", "111"},
        {"BBB", "222"},
        {"CCC", "333"},
      };
      struct header input2[] = {{"DDD", "444"}, {"DDD", "555"}, {"FFF", "666"}};

      hdr1.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name.data(), entry.name.length());
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name.data(), entry.name.length());
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "111", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "222", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("CCC", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "333", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "444", len) == 0);
      CHECK(field->has_dups() == true);

      field = hdr1.field_find("FFF", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "666", len) == 0);
      CHECK(field->has_dups() == false);
    }

    SECTION("Have common and dup headers")
    {
      HTTPHdr hdr1;
      HTTPHdr hdr2;
      MIMEField *field;
      const char *str;
      int len;

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
        {"AAA", "555"}, {"BBB", "666"}, {"BBB", "777"}, {"CCC", "888"}, {"EEE", "999"},
      };

      hdr1.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input1) {
        field = hdr1.field_create(entry.name.data(), entry.name.length());
        hdr1.field_attach(field);
        hdr1.field_value_set(field, entry.value.data(), entry.value.length());
      }

      hdr2.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : input2) {
        field = hdr2.field_create(entry.name.data(), entry.name.length());
        hdr2.field_attach(field);
        hdr2.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&hdr1, &hdr2);

      CHECK(hdr1.fields_count() == 6);

      field = hdr1.field_find("AAA", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "555", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("BBB", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "666", len) == 0);
      CHECK(field->has_dups() == true);

      ///////////// Dup //////////////////////////
      field = field->m_next_dup;
      str   = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "777", len) == 0);
      CHECK(field->has_dups() == false);
      ///////////////////////////////////////

      field = hdr1.field_find("CCC", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "888", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("DDD", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "444", len) == 0);
      CHECK(field->has_dups() == false);

      field = hdr1.field_find("EEE", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "999", len) == 0);
      CHECK(field->has_dups() == false);
    }
    SECTION("Response has superset")
    {
      HTTPHdr cached_headers;
      HTTPHdr response_headers;
      MIMEField *field;
      const char *str;
      int len;

      struct header {
        std::string_view name;
        std::string_view value;
      };

      struct header cached[] = {
        {"Foo", "111"}, {"Fizz", "555"}, {"Bar", "333"}, {"Bop", "666"}, {"Bar", "222"}, {"X-Foo", "aaa"}, {"Eat", "444"},
      };
      // Response headers in a 304 should, in theory, match the cached headers, but, what if they don't?
      // The response headers should still be merged into the cached object properly given the existing logic.
      // In the following, the ordering is different from the cached headers, the Bar headers are missing, and two duplicate Zip
      // headers are not in the cached object.
      struct header response[] = {
        {"X-Foo", "aaa"}, {"Zip", "888"}, {"Zip", "999"}, {"Eat", "444"}, {"Foo", "111"}, {"Fizz", "555"}, {"Bop", "666"},
      };

      cached_headers.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : cached) {
        field = cached_headers.field_create(entry.name.data(), entry.name.length());
        cached_headers.field_attach(field);
        cached_headers.field_value_set(field, entry.value.data(), entry.value.length());
      }

      response_headers.create(HTTP_TYPE_RESPONSE);
      for (auto &&entry : response) {
        field = response_headers.field_create(entry.name.data(), entry.name.length());
        response_headers.field_attach(field);
        response_headers.field_value_set(field, entry.value.data(), entry.value.length());
      }

      HttpTransact::merge_response_header_with_cached_header(&cached_headers, &response_headers);

      CHECK(cached_headers.fields_count() == 9);
      CHECK(response_headers.fields_count() == 7);

      field = cached_headers.field_find("Foo", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "111", len) == 0);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Fizz", 4);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "555", len) == 0);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Bop", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "666", len) == 0);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("X-Foo", 5);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "aaa", len) == 0);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Eat", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "444", len) == 0);
      CHECK(field->has_dups() == false);

      field = cached_headers.field_find("Bar", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "333", len) == 0);
      CHECK(field->has_dups() == true);

      ///////////// Dup //////////////////////////
      field = field->m_next_dup;
      str   = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "222", len) == 0);
      CHECK(field->has_dups() == false);
      ///////////////////////////////////////

      field = cached_headers.field_find("Zip", 3);
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "888", len) == 0);
      CHECK(field->has_dups() == true);

      ///////////// Dup //////////////////////////
      REQUIRE(field->m_next_dup != nullptr);
      field = field->m_next_dup;
      REQUIRE(field != nullptr);
      str = field->value_get(&len);
      CHECK(len == 3);
      CHECK(strncmp(str, "999", len) == 0);
      CHECK(field->has_dups() == false);
      ///////////////////////////////////////
    }
  }
}
