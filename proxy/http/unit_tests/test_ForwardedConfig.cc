/** @file

  Catch-based tests for ForwardedConfig.cc.

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

#include <string>
#include <cstring>
#include <cctype>
#include <bitset>
#include <initializer_list>

#include "catch.hpp"

#include "HttpConfig.h"

using namespace HttpForwarded;

class OptionBitSetListInit : public OptionBitSet
{
public:
  explicit OptionBitSetListInit(std::initializer_list<std::size_t> il)
  {
    for (std::size_t i : il) {
      this->set(i);
    }
  }
};

namespace
{
const char *wsTbl[] = {"", " ", "  ", nullptr};

int wsIdx{0};

const char *
nextWs()
{
  ++wsIdx;

  if (!wsTbl[wsIdx]) {
    wsIdx = 0;
  }

  return wsTbl[wsIdx];
}
// Alternate upper/lower case and add blanks.
class XS
{
private:
  std::string s;

public:
  explicit XS(const char *in) : s{nextWs()}
  {
    bool upper{true};
    for (; *in; ++in) {
      if (islower(*in)) {
        s += upper ? toupper(*in) : *in;
        upper = !upper;

      } else if (isupper(*in)) {
        s += upper ? *in : tolower(*in);
        upper = !upper;

      } else {
        s += *in;
      }
      s += nextWs();
    }
    s += nextWs();
  }

  operator std::string_view() const { return std::string_view(s.c_str()); }
};

void
test(const char *spec, const char *reqErr, const OptionBitSet &bS)
{
  ts::LocalBufferWriter<1024> error;

  error << "cheese";

  REQUIRE(bS == optStrToBitset(XS(spec), error));
  std::size_t len = std::strlen(reqErr);
  REQUIRE((error.size() - sizeof("cheese") + 1) == len);
  REQUIRE(std::memcmp(error.data() + sizeof("cheese") - 1, reqErr, len) == 0);
}

} // namespace

TEST_CASE("Forwarded", "[FWD]")
{
  test("none", "", OptionBitSet());

  test("", "\"Forwarded\" configuration: \"   \" is a bad option.", OptionBitSet());

  test("\t", "\"Forwarded\" configuration: \"\t   \" is a bad option.", OptionBitSet());

  test(":", "\"Forwarded\" configuration: \"\" and \"   \" are bad options.", OptionBitSet());

  test("|", "\"Forwarded\" configuration: \"\" and \"   \" are bad options.", OptionBitSet());

  test("by=ip", "", OptionBitSetListInit{BY_IP});

  test("by=unknown", "", OptionBitSetListInit{BY_UNKNOWN});

  test("by=servername", "", OptionBitSetListInit{BY_SERVER_NAME});

  test("by=uuid", "", OptionBitSetListInit{BY_UUID});

  test("for", "", OptionBitSetListInit{FOR});

  test("proto", "", OptionBitSetListInit{PROTO});

  test("host", "", OptionBitSetListInit{HOST});

  test("connection=compact", "", OptionBitSetListInit{CONNECTION_COMPACT});

  test("connection=standard", "", OptionBitSetListInit{CONNECTION_STD});

  test("connection=std", "", OptionBitSetListInit{CONNECTION_STD});

  test("connection=full", "", OptionBitSetListInit{CONNECTION_FULL});

  test("proto:by=uuid|for", "", OptionBitSetListInit{PROTO, BY_UUID, FOR});

  test("proto:by=cheese|fur", "\"Forwarded\" configuration: \" b  Y= c  He E  sE \" and \"  fU r  \" are bad options.",
       OptionBitSet());

  test("proto:by=cheese|fur|compact=",
       "\"Forwarded\" configuration: \" b  Y= c  He E  sE \", \"  fU r  \" and \"C o  Mp A  cT =  \" are bad options.",
       OptionBitSet());

#undef X
#define X(S)                                                                                                                  \
  "by=ip" S "by=unknown" S "by=servername" S "by=uuid" S "for" S "proto" S "host" S "connection=compact" S "connection=std" S \
  "connection=full"

  test(X(":"), "", OptionBitSet().set());

  test(X("|"), "", OptionBitSet().set());

  test(X("|") "|" X(":"), "", OptionBitSet().set());

  test(X("|") ":abcd", "\"Forwarded\" configuration: \"  aB c  D \" is a bad option.", OptionBitSet());

  test(X("|") ":for=abcd", "\"Forwarded\" configuration: \" f  Or =  Ab C  d \" is a bad option.", OptionBitSet());

  test(X("|") ":by", "\"Forwarded\" configuration: \" b  Y \" is a bad option.", OptionBitSet());
}
