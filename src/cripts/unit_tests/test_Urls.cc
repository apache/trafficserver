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

#include "CriptsTestTxn.h"

using cripts_test::str;
using cripts_test::Txn;

namespace
{
constexpr std::string_view ABSOLUTE_REQUEST = "GET http://example.com:8080/a/b/c?x=1&y=2 HTTP/1.1\r\nHost: example.com\r\n\r\n";

} // namespace

TEST_CASE("Url: components read from the request", "[cripts][Urls]")
{
  Txn  txn{ABSOLUTE_REQUEST};
  auto context = txn.context();

  borrow url = cripts::Client::URL::Get();

  CHECK(str(url.scheme) == "http");
  CHECK(str(url.host) == "example.com");
  CHECK(integer(url.port) == 8080);
  CHECK(str(url.path) == "a/b/c");
  CHECK(str(url.query) == "x=1&y=2");
  CHECK(str(url.query["y"]) == "2");
  CHECK(str(url.query["z"]).empty());
  CHECK(str(url.path[1]) == "b");
  CHECK_FALSE(url.Modified());
}

TEST_CASE("Url: whole component assignment reaches the core", "[cripts][Urls]")
{
  Txn  txn{ABSOLUTE_REQUEST};
  auto context = txn.context();

  borrow url = cripts::Client::URL::Get();

  url.host  = "other.example.com";
  url.path  = "z";
  url.query = "q=9";
  CHECK(url.Modified());
  CHECK(txn.client_url().host_get() == "other.example.com");
  CHECK(txn.client_url().path_get() == "z");
  CHECK(txn.client_url().query_get() == "q=9");
  CHECK(str(url.host) == "other.example.com");
  CHECK(str(url.path) == "z");
  CHECK(str(url.query) == "q=9");
}

TEST_CASE("Url: the cache URL reads every component again in the next hook", "[cripts][Urls]")
{
  Txn  txn{ABSOLUTE_REQUEST};
  auto context = txn.context();

  // Both hooks run before the cache lookup, where the cache URL is a copy of the client URL.
  context->state.hook = TS_HTTP_READ_REQUEST_HDR_HOOK;

  {
    borrow url = cripts::Cache::URL::Get();

    CHECK(str(url.scheme) == "http");
    CHECK(str(url.host) == "example.com");
    CHECK(integer(url.port) == 8080);
    CHECK(str(url.path) == "a/b/c");
    CHECK(str(url.query) == "x=1&y=2");
  }

  // Remap rewrites the client URL between the two hooks.
  txn.client_url().scheme_set("https");
  txn.client_url().host_set("other.example");
  txn.client_url().port_set(8443);
  txn.client_url().path_set("d");
  txn.client_url().query_set("z=3");

  context->reset();
  context->state.hook = TS_HTTP_POST_REMAP_HOOK;

  borrow url = cripts::Cache::URL::Get();

  CHECK(str(url.scheme) == "https");
  CHECK(str(url.host) == "other.example");
  CHECK(integer(url.port) == 8443);
  CHECK(str(url.path) == "d");
  CHECK(str(url.query) == "z=3");
}

TEST_CASE("Url: the client URL reads every component again in the next hook", "[cripts][Urls]")
{
  Txn  txn{ABSOLUTE_REQUEST};
  auto context = txn.context();

  context->state.hook = TS_HTTP_READ_REQUEST_HDR_HOOK;

  {
    borrow url = cripts::Client::URL::Get();

    CHECK(str(url.scheme) == "http");
    CHECK(str(url.host) == "example.com");
    CHECK(integer(url.port) == 8080);
    CHECK(str(url.path) == "a/b/c");
    CHECK(str(url.query) == "x=1&y=2");
  }

  // Remap rewrites the client URL between the two hooks.
  txn.client_url().scheme_set("https");
  txn.client_url().host_set("other.example");
  txn.client_url().port_set(8443);
  txn.client_url().path_set("d");
  txn.client_url().query_set("z=3");

  context->reset();
  context->state.hook = TS_HTTP_POST_REMAP_HOOK;

  borrow url = cripts::Client::URL::Get();

  CHECK(str(url.scheme) == "https");
  CHECK(str(url.host) == "other.example");
  CHECK(integer(url.port) == 8443);
  CHECK(str(url.path) == "d");
  CHECK(str(url.query) == "z=3");
}

TEST_CASE("Url: erasing every query parameter reads an empty query", "[cripts][Urls]")
{
  Txn  txn{ABSOLUTE_REQUEST};
  auto context = txn.context();

  borrow url = cripts::Client::URL::Get();

  SECTION("by name")
  {
    url.query.Erase("x");
    url.query.Erase("y");
    CHECK(str(url.query).empty());
  }

  SECTION("keeping none of them")
  {
    url.query.Erase({"z"}, true);
    CHECK(str(url.query).empty());
  }
}
