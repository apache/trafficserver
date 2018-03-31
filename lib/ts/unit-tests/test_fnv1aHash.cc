/** @file

    Unit tests for fnv1aHash.h and Series.h .

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

#include <cstdint>
#include <string>

#include <ts/fnv1aHash.h>
#include <ts/string_view.h>

struct A {
  ts::string_view sv1, sv2;
  int i;
};

struct B {
  ts::string_view sv;
  std::string str;
  const char *cStr;
  int i;
};

namespace ts
{
template <typename Accum> struct Series<Accum, A> {
  static void
  visit(Accum &acc, const A &a)
  {
    Series<Accum, string_view>::visit(acc, a.sv1);
    Series<Accum, string_view>::visit(acc, a.sv2);
    Series<Accum, int>::visit(acc, a.i);
  }
};

template <typename Accum> struct Series<Accum, B> {
  static void
  visit(Accum &acc, const B &b)
  {
    Series<Accum, string_view>::visit(acc, b.sv);
    Series<Accum, std::string>::visit(acc, b.str);
    Series<Accum, const char *>::visit(acc, b.cStr);
    Series<Accum, int>::visit(acc, b.i);
  }
};

} // end namespace ts

TEST_CASE("fnv1aHash", "[FNV1A]")
{
  ts::string_view sv("Aprendo de mis pasos, entiendo en mi caminar");

  REQUIRE(ts::fnv1aHash(sv) == ((static_cast<std::uint64_t>(0x4cdb1b64) << 32) bitor 0x9de499f4));

  std::uint64_t SameHash = (static_cast<std::uint64_t>(0xcc507284) << 32) bitor 0xf5c404fc;

  A a = {"Aprendo de mis pasos, ", "entiendo en mi caminar", 0x12345678};

  REQUIRE(ts::fnv1aHash(a) == SameHash);

  B b = {"Aprendo de mis pasos, ", "entiendo en ", "mi caminar", 0x12345678};

  REQUIRE(ts::fnv1aHash(b) == SameHash);
}
