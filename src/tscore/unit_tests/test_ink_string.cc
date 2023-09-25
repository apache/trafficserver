/** @file

    test ink_string.h - string utility functions

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

#include <tscore/ink_string.h>
#include <tscore/ParseRules.h>
#include <catch.hpp>
#include <string_view>

//-------------------------------------------------------------------------
// ink_fast_ltoa test
//-------------------------------------------------------------------------
struct int64_item {
  int64_t n;
  std::string_view s;
};

constexpr int64_item int64_tests[] = {
  {{0},                    {"0"}                   },
  {{1},                    {"1"}                   },
  {{10},                   {"10"}                  },
  {{100},                  {"100"}                 },
  {{1000},                 {"1000"}                },
  {{10000},                {"10000"}               },
  {{100000},               {"100000"}              },
  {{1000000},              {"1000000"}             },
  {{10000000},             {"10000000"}            },
  {{100000000},            {"100000000"}           },
  {{1000000000},           {"1000000000"}          },
  {{10000000000},          {"10000000000"}         },
  {{100000000000},         {"100000000000"}        },
  {{1000000000000},        {"1000000000000"}       },
  {{10000000000000},       {"10000000000000"}      },
  {{100000000000000},      {"100000000000000"}     },
  {{1000000000000000},     {"1000000000000000"}    },
  {{10000000000000000},    {"10000000000000000"}   },
  {{100000000000000000},   {"100000000000000000"}  },
  {{1000000000000000000},  {"1000000000000000000"} },
  {{-1},                   "-1"                    },
  {{-10},                  "-10"                   },
  {{-100},                 "-100"                  },
  {{-1000},                "-1000"                 },
  {{-10000},               "-10000"                },
  {{-100000},              "-100000"               },
  {{-1000000},             "-1000000"              },
  {{-10000000},            "-10000000"             },
  {{-100000000},           "-100000000"            },
  {{-1000000000},          "-1000000000"           },
  {{-10000000000},         "-10000000000"          },
  {{-100000000000},        "-100000000000"         },
  {{-1000000000000},       "-1000000000000"        },
  {{-10000000000000},      "-10000000000000"       },
  {{-100000000000000},     "-100000000000000"      },
  {{-1000000000000000},    "-1000000000000000"     },
  {{-10000000000000000},   "-10000000000000000"    },
  {{-100000000000000000},  "-100000000000000000"   },
  {{-1000000000000000000}, "-1000000000000000000"  },
  {{INT64_MAX},            {"9223372036854775807"} },
  {{INT64_MIN},            {"-9223372036854775808"}},
};

TEST_CASE("ink_fast_ltoa", "[libts][ink_fast_ltoa]")
{
  printf("ink_string\n");
  char buffer[21];
  for (auto const &test : int64_tests) {
    REQUIRE(ink_atoi64(test.s.data()) == test.n);
    int length = 0;
    REQUIRE((length = ink_fast_ltoa(test.n, buffer, sizeof(buffer))) == (int)test.s.length());
    REQUIRE(std::string_view(buffer, length) == test.s);
  }
}

//-------------------------------------------------------------------------
// ink_fast_inta test
//-------------------------------------------------------------------------
struct int_item {
  int n;
  std::string_view s;
};

constexpr int_item int_tests[] = {
  {{0},           {"0"}          },
  {{1},           {"1"}          },
  {{10},          {"10"}         },
  {{100},         {"100"}        },
  {{1000},        {"1000"}       },
  {{10000},       {"10000"}      },
  {{100000},      {"100000"}     },
  {{1000000},     {"1000000"}    },
  {{10000000},    {"10000000"}   },
  {{100000000},   {"100000000"}  },
  {{1000000000},  {"1000000000"} },
  {{-1},          {"-1"}         },
  {{-10},         {"-10"}        },
  {{-100},        {"-100"}       },
  {{-1000},       {"-1000"}      },
  {{-10000},      {"-10000"}     },
  {{-100000},     {"-100000"}    },
  {{-1000000},    {"-1000000"}   },
  {{-10000000},   {"-10000000"}  },
  {{-100000000},  {"-100000000"} },
  {{-1000000000}, {"-1000000000"}},
  {{INT_MAX},     {"2147483647"} },
  {{INT_MIN},     {"-2147483648"}},
};

TEST_CASE("ink_fast_inta", "[libts][ink_fast_inta]")
{
  char buffer[12];
  for (auto const &test : int_tests) {
    REQUIRE(ink_atoi(test.s.data()) == test.n);
    int length = 0;
    REQUIRE((length = ink_fast_itoa(test.n, buffer, sizeof(buffer))) == (int)test.s.length());
    REQUIRE(std::string_view(buffer, length) == test.s);
  }
}

//-------------------------------------------------------------------------
// ink_fast_uinta test
//-------------------------------------------------------------------------
struct uint_item {
  unsigned int n;
  std::string_view s;
};

constexpr uint_item uint_tests[] = {
  {{0},          {"0"}         },
  {{1},          {"1"}         },
  {{10},         {"10"}        },
  {{100},        {"100"}       },
  {{1000},       {"1000"}      },
  {{10000},      {"10000"}     },
  {{100000},     {"100000"}    },
  {{1000000},    {"1000000"}   },
  {{10000000},   {"10000000"}  },
  {{100000000},  {"100000000"} },
  {{1000000000}, {"1000000000"}},
  {{UINT_MAX},   {"4294967295"}},
};

TEST_CASE("ink_fast_uinta", "[libts][ink_fast_uinta]")
{
  char buffer[12];
  for (auto const &test : uint_tests) {
    REQUIRE(ink_atoui(test.s.data()) == test.n);
    int length = 0;
    REQUIRE((length = ink_fast_uitoa(test.n, buffer, sizeof(buffer))) == (int)test.s.length());
    REQUIRE(std::string_view(buffer, length) == test.s);
  }
}
