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

#include <string>
#include <vector>

using cripts_test::str;
using cripts_test::Txn;

namespace
{
constexpr std::string_view REQUEST = "GET /a HTTP/1.1\r\nHost: example.com\r\nX-Foo: bar\r\nX-Dup: 1\r\nX-Dup: 2\r\n\r\n";
} // namespace

TEST_CASE("Header: read fields", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  borrow req = cripts::Client::Request::Get();

  CHECK(str(req["X-Foo"]) == "bar");
  CHECK(str(req["x-foo"]) == "bar");
  CHECK(str(req["Host"]) == "example.com");
  CHECK(str(req["X-Missing"]).empty());
  CHECK(str(req.method) == "GET");
  CHECK((req.method == cripts::Method::GET));
  CHECK((req.method != cripts::Method::POST));
}

TEST_CASE("Header: add a field", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  borrow req = cripts::Client::Request::Get();

  req["X-New"] = "value";
  CHECK(txn.client_request_field("X-New") == "value");
  CHECK(str(req["X-New"]) == "value");
}

TEST_CASE("Header: replacing a field removes its duplicates", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  borrow req = cripts::Client::Request::Get();

  req["X-Dup"] = "3";
  CHECK(txn.client_request_field_count("X-Dup") == 1);
  CHECK(txn.client_request_field("X-Dup") == "3");
}

TEST_CASE("Header: assigning an empty value erases the field and its duplicates", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  borrow req = cripts::Client::Request::Get();

  req["X-Dup"] = "";
  CHECK(txn.client_request_field_count("X-Dup") == 0);

  req.Erase("X-Foo");
  CHECK(txn.client_request_field_count("X-Foo") == 0);

  req["X-Missing"] = "";
  CHECK(txn.client_request_field_count("X-Missing") == 0);
}

TEST_CASE("Header: integer values", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  borrow req = cripts::Client::Request::Get();

  req["X-Int"] = integer(42);
  CHECK(txn.client_request_field("X-Int") == "42");

  req["X-Foo"] = integer(-7);
  CHECK(txn.client_request_field("X-Foo") == "-7");
  CHECK(req["X-Int"].ToInteger() == 42);
}

TEST_CASE("Header: += adds another field line", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  borrow req = cripts::Client::Request::Get();

  req["X-Foo"] += "baz";
  CHECK(txn.client_request_field_count("X-Foo") == 2);

  req["X-Other"] += "first";
  CHECK(txn.client_request_field("X-Other") == "first");
}

TEST_CASE("Header: iterate over the field names", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  borrow                   req = cripts::Client::Request::Get();
  std::vector<std::string> names;

  for (auto name : req) {
    names.emplace_back(name.GetSV());
  }
  CHECK(names == std::vector<std::string>{"Host", "X-Foo", "X-Dup", "X-Dup"});
}

TEST_CASE("Header: the next hook sees the current headers", "[cripts][Headers]")
{
  Txn  txn{REQUEST};
  auto context = txn.context();

  CHECK(str(cripts::Client::Request::Get()["X-Foo"]) == "bar");
  context->reset();

  txn.sm().t_state.hdr_info.client_request.value_set("X-Foo"sv, "changed"sv);

  CHECK(str(cripts::Client::Request::Get()["X-Foo"]) == "changed");
}

TEST_CASE("Header: server response status", "[cripts][Headers]")
{
  Txn txn{REQUEST};

  txn.set_server_response("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");

  auto context = txn.context();

  context->state.hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;

  borrow resp = cripts::Server::Response::Get();

  CHECK(integer(resp.status) == 404);
  resp.status = 200;
  CHECK(static_cast<int>(txn.sm().t_state.hdr_info.server_response.status_get()) == 200);
}
