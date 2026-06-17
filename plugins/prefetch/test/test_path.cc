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

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "../path.h"

TEST_CASE("Safe prefetch paths stay under the current directory", "[prefetch][path]")
{
  SafeRelativeFetchPath fetchPath;

  REQUIRE(makeSafeRelativeFetchPath("texts/demo-1.txt", "demo-2.txt", fetchPath));
  REQUIRE(fetchPath.path == "texts/demo-2.txt");
  REQUIRE_FALSE(fetchPath.hasQuery);

  REQUIRE(makeSafeRelativeFetchPath("texts/demo-1.txt", "./demo-2.txt", fetchPath));
  REQUIRE(fetchPath.path == "texts/demo-2.txt");
  REQUIRE_FALSE(fetchPath.hasQuery);

  REQUIRE(makeSafeRelativeFetchPath("texts/demo-1.txt", "segments/../demo-2.txt", fetchPath));
  REQUIRE(fetchPath.path == "texts/demo-2.txt");
  REQUIRE_FALSE(fetchPath.hasQuery);

  REQUIRE(makeSafeRelativeFetchPath("tests/query", "query?bar=baz", fetchPath));
  REQUIRE(fetchPath.path == "tests/query");
  REQUIRE(fetchPath.hasQuery);
  REQUIRE(fetchPath.query == "bar=baz");

  REQUIRE(makeSafeRelativeFetchPath("root.txt", "rooted", fetchPath));
  REQUIRE(fetchPath.path == "rooted");
  REQUIRE_FALSE(fetchPath.hasQuery);
}

TEST_CASE("Unsafe prefetch paths cannot escape the current directory", "[prefetch][path]")
{
  SafeRelativeFetchPath fetchPath;

  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "?bar=baz", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "/demo-2.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "/foo/../../bar", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "\\demo-2.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "%2fdemo-2.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "demo-2.txt#fragment", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "../private/secret.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "foo/../../bar", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "segments/../../private/secret.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "%2e%2e/private/secret.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "..%2fprivate/secret.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("texts/demo-1.txt", "..\\private\\secret.txt", fetchPath));
  REQUIRE_FALSE(makeSafeRelativeFetchPath("root.txt", "../private/secret.txt", fetchPath));
}
