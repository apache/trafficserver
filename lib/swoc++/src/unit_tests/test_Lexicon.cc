/** @file

    Lexicon unit tests.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "swoc/Lexicon.h"
#include "swoc/ext/catch.hpp"

// Example code for documentatoin
// ---

enum class Example { INVALID, Value_0, Value_1, Value_2, Value_3 };

using ExampleNames = swoc::Lexicon<Example>;

TEST_CASE("Lexicon Example", "[libts][Lexicon]")
{
  ExampleNames exnames{{Example::Value_0, {"zero", "0"}},
                       {Example::Value_1, {"one", "1"}},
                       {Example::Value_2, {"two", "2"}},
                       {Example::Value_3, {"three", "3"}},
                       {Example::INVALID, {"INVALID"}}};

  ExampleNames exnames2{{Example::Value_0, "zero"},
                        {Example::Value_1, "one"},
                        {Example::Value_2, "two"},
                        {Example::Value_3, "three"},
                        {Example::INVALID, "INVALID"}};

  exnames.set_default(Example::INVALID).set_default("INVALID");

  REQUIRE(exnames[Example::INVALID] == "INVALID");
  REQUIRE(exnames[Example::Value_0] == "zero");
  REQUIRE(exnames["zero"] == Example::Value_0);
  REQUIRE(exnames["Zero"] == Example::Value_0);
  REQUIRE(exnames["ZERO"] == Example::Value_0);
  REQUIRE(exnames["one"] == Example::Value_1);
  REQUIRE(exnames["1"] == Example::Value_1);
  REQUIRE(exnames["Evil Dave"] == Example::INVALID);
  REQUIRE(exnames[static_cast<Example>(0xBADD00D)] == "INVALID");
  REQUIRE(exnames[exnames[static_cast<Example>(0xBADD00D)]] == Example::INVALID);

  // Case of an enumeration with a "LAST_VALUE".
  enum class Radio { INVALID, ALPHA, BRAVO, CHARLIE, DELTA, LAST_VALUE };
  using Lex = swoc::Lexicon<Radio>;
  Lex lex(Lex::Require<Radio::LAST_VALUE>(), {{{Radio::INVALID, {"Invalid"}},
                                               {Radio::ALPHA, {"Alpha"}},
                                               {Radio::BRAVO, {"Bravo", "Beta"}},
                                               {Radio::CHARLIE, {"Charlie"}},
                                               {Radio::DELTA, {"Delta"}}}});
};

// ---
// End example code.

enum Values { NoValue, LowValue, HighValue, Priceless };
enum Hex { A, B, C, D, E, F, INVALID };

using ValueLexicon = swoc::Lexicon<Values>;

TEST_CASE("Lexicon Constructor", "[libts][Lexicon]")
{
  // Construct with a secondary name for NoValue
  ValueLexicon vl{{NoValue, {"NoValue", "garbage"}}, {LowValue, {"LowValue"}}};

  REQUIRE("LowValue" == vl[LowValue]);                 // Primary name
  REQUIRE(NoValue == vl["NoValue"]);                   // Primary name
  REQUIRE(NoValue == vl["garbage"]);                   // Secondary name
  REQUIRE_THROWS_AS(vl["monkeys"], std::domain_error); // No default, so throw.
  vl.set_default(NoValue);                             // Put in a default.
  REQUIRE(NoValue == vl["monkeys"]);                   // Returns default instead of throw
  REQUIRE(LowValue == vl["lowVALUE"]);                 // Check case insensitivity.

  REQUIRE(NoValue == vl["HighValue"]);               // Not defined yet.
  vl.define(HighValue, {"HighValue", "High_Value"}); // Add it.
  REQUIRE(HighValue == vl["HighValue"]);             // Verify it's there and is case insensitive.
  REQUIRE(HighValue == vl["highVALUE"]);
  REQUIRE(HighValue == vl["HIGH_VALUE"]);
  REQUIRE("HighValue" == vl[HighValue]); // Verify value -> primary name.

  // A few more checks on primary/secondary.
  REQUIRE(NoValue == vl["Priceless"]);
  REQUIRE(NoValue == vl["unique"]);
  vl.define(Priceless, "Priceless", "Unique");
  REQUIRE("Priceless" == vl[Priceless]);
  REQUIRE(Priceless == vl["unique"]);

  // Check default handlers.
  using LL         = swoc::Lexicon<Hex>;
  bool bad_value_p = false;
  LL ll_1({{A, "A"}, {B, "B"}, {C, "C"}, {E, "E"}});
  ll_1.set_default([&bad_value_p](std::string_view name) mutable -> Hex {
    bad_value_p = true;
    return INVALID;
  });
  ll_1.set_default([&bad_value_p](Hex value) mutable -> std::string_view {
    bad_value_p = true;
    return "INVALID";
  });
  REQUIRE(bad_value_p == false);
  REQUIRE(INVALID == ll_1["F"]);
  REQUIRE(bad_value_p == true);
  bad_value_p = false;
  REQUIRE("INVALID" == ll_1[F]);
  REQUIRE(bad_value_p == true);
  bad_value_p = false;
  // Verify that INVALID / "INVALID" are equal because of the default handlers.
  REQUIRE("INVALID" == ll_1[INVALID]);
  REQUIRE(INVALID == ll_1["INVALID"]);
  REQUIRE(bad_value_p == true);
  // Define the value/name, verify the handlers are *not* invoked.
  ll_1.define(INVALID, "INVALID");
  bad_value_p = false;
  REQUIRE("INVALID" == ll_1[INVALID]);
  REQUIRE(INVALID == ll_1["INVALID"]);
  REQUIRE(bad_value_p == false);

  ll_1.define({D, "D"});          // Pair style
  ll_1.define({F, {"F", "0xf"}}); // Definition style
  REQUIRE(ll_1[D] == "D");
  REQUIRE(ll_1["0XF"] == F);

  // iteration
  std::bitset<INVALID + 1> mark;
  for (auto [value, name] : ll_1) {
    if (mark[value]) {
      std::cerr << "Lexicon: " << name << ':' << value << " double iterated" << std::endl;
      mark.reset();
      break;
    }
    mark[value] = true;
  }
  REQUIRE(mark.all());
};
