/** @file

  Frozen JAWS v1 encoder for stripped JA3 raw strings.

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

#include "bitset_hex.h"
#include "ja3/ja3_fingerprints.h"
#include "jaws.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

using namespace std::literals;

namespace
{
constexpr char JA3_part_item_delimiter{'-'};
constexpr char JA3_part_delimiter{','};
constexpr char JAWS_part_delimiter{'|'};
constexpr char JAWS_part_size_delimiter{'-'};

class TokenStream
{
public:
  TokenStream(std::string_view view, char delimiter) : raw_content{view}, token_delimiter{delimiter} {}

  std::string_view
  consume()
  {
    std::size_t const token_end{this->raw_content.find(this->token_delimiter, this->token_begin)};
    std::size_t const token_size{token_end - token_begin};
    std::string_view  result{this->raw_content.substr(this->token_begin, token_size)};
    if (token_end != std::string_view::npos) {
      this->token_begin = token_end + 1;
    } else {
      this->token_begin = this->raw_content.length();
    }
    return result;
  }

  void
  skip()
  {
    std::size_t const token_end{this->raw_content.find(this->token_delimiter, this->token_begin)};
    this->token_begin = token_end + 1;
  }

  bool
  empty() const
  {
    return this->token_begin >= this->raw_content.length();
  }

private:
  std::string_view raw_content;
  char             token_delimiter;
  std::size_t      token_begin{0};
};

namespace anchor_order
{
  // clang-format off
  constexpr std::array<std::string_view, 130> cipher_suites{
    "_"sv,
    "49172"sv,
    "49171"sv,
    "49196"sv,
    "49195"sv,
    "53"sv,
    "47"sv,
    "49200"sv,
    "49199"sv,
    "157"sv,
    "156"sv,
    "49162"sv,
    "49161"sv,
    "49191"sv,
    "49188"sv,
    "49187"sv,
    "52393"sv,
    "52392"sv,
    "49192"sv,
    "61"sv,
    "60"sv,
    "4866"sv,
    "4865"sv,
    "159"sv,
    "158"sv,
    "4867"sv,
    "10"sv,
    "56"sv,
    "50"sv,
    "255"sv,
    "57"sv,
    "51"sv,
    "64"sv,
    "106"sv,
    "107"sv,
    "103"sv,
    "52394"sv,
    "163"sv,
    "162"sv,
    "19"sv,
    "49202"sv,
    "49201"sv,
    "49198"sv,
    "49197"sv,
    "49194"sv,
    "49193"sv,
    "49190"sv,
    "49189"sv,
    "49167"sv,
    "49166"sv,
    "49157"sv,
    "49156"sv,
    "49170"sv,
    "5"sv,
    "49160"sv,
    "49327"sv,
    "49326"sv,
    "49325"sv,
    "49324"sv,
    "49315"sv,
    "49314"sv,
    "49313"sv,
    "49312"sv,
    "49311"sv,
    "49310"sv,
    "49309"sv,
    "49308"sv,
    "4"sv,
    "22"sv,
    "49169"sv,
    "69"sv,
    "65"sv,
    "49165"sv,
    "49159"sv,
    "49155"sv,
    "136"sv,
    "132"sv,
    "68"sv,
    "154"sv,
    "153"sv,
    "150"sv,
    "135"sv,
    "67"sv,
    "66"sv,
    "63"sv,
    "62"sv,
    "55"sv,
    "54"sv,
    "49249"sv,
    "49248"sv,
    "49245"sv,
    "49244"sv,
    "49239"sv,
    "49238"sv,
    "49235"sv,
    "49234"sv,
    "49233"sv,
    "49232"sv,
    "49"sv,
    "48"sv,
    "165"sv,
    "164"sv,
    "161"sv,
    "160"sv,
    "152"sv,
    "151"sv,
    "134"sv,
    "133"sv,
    "105"sv,
    "104"sv,
    "7"sv,
    "196"sv,
    "192"sv,
    "190"sv,
    "186"sv,
    "16"sv,
    "13"sv,
    "9"sv,
    "65413"sv,
    "49271"sv,
    "49270"sv,
    "49267"sv,
    "49266"sv,
    "49164"sv,
    "49154"sv,
    "4868"sv,
    "21"sv,
    "195"sv,
    "189"sv,
    "129"sv,
  };
  constexpr std::array<std::string_view, 46> extensions{
    "_"sv,
    "23"sv,
    "24"sv,
    "25"sv,
    "22"sv,
    "9"sv,
    "14"sv,
    "13"sv,
    "12"sv,
    "11"sv,
    "10"sv,
    "21"sv,
    "19"sv,
    "20"sv,
    "18"sv,
    "1"sv,
    "16"sv,
    "17"sv,
    "15"sv,
    "8"sv,
    "7"sv,
    "6"sv,
    "5"sv,
    "4"sv,
    "3"sv,
    "2"sv,
    "29"sv,
    "30"sv,
    "26"sv,
    "28"sv,
    "27"sv,
    "257"sv,
    "256"sv,
    "260"sv,
    "259"sv,
    "258"sv,
    "16696"sv,
    "249"sv,
    "0"sv,
    "35"sv,
    "65281"sv,
    "50"sv,
    "43"sv,
    "51"sv,
    "45"sv,
    "41"sv,
  };
  constexpr std::array<std::string_view, 37> elliptic_curves{
    "_"sv,
    "23"sv,
    "24"sv,
    "25"sv,
    "29"sv,
    "22"sv,
    "30"sv,
    "9"sv,
    "14"sv,
    "13"sv,
    "12"sv,
    "11"sv,
    "10"sv,
    "257"sv,
    "256"sv,
    "28"sv,
    "27"sv,
    "26"sv,
    "21"sv,
    "19"sv,
    "16"sv,
    "8"sv,
    "7"sv,
    "6"sv,
    "5"sv,
    "4"sv,
    "3"sv,
    "20"sv,
    "2"sv,
    "18"sv,
    "17"sv,
    "15"sv,
    "1"sv,
    "260"sv,
    "259"sv,
    "258"sv,
    "16696"sv,
  };
  // clang-format on

  template <typename T>
  std::ptrdiff_t
  find_anchor_index(T const &anchor_arr, std::string_view sv)
  {
    if (auto const it{std::find(anchor_arr.begin() + 1, anchor_arr.end(), sv)}; it != anchor_arr.end()) {
      return std::distance(anchor_arr.begin(), it);
    }
    return std::ptrdiff_t{0};
  }
} // namespace anchor_order

template <std::size_t N>
std::bitset<N>
reduce_JA3_part_to_bitfield(std::array<std::string_view, N> const &anchor_arr, std::string_view JA3_part)
{
  std::bitset<N> result;
  TokenStream    tokens{JA3_part, JA3_part_item_delimiter};
  while (!tokens.empty()) {
    result.set(anchor_order::find_anchor_index(anchor_arr, tokens.consume()));
  }
  return result;
}

template <std::size_t N>
std::string
encode_score(std::array<std::string_view, N> const &anchor_arr, std::string_view JA3_part)
{
  auto const  bits{reduce_JA3_part_to_bitfield(anchor_arr, JA3_part)};
  std::string result{std::to_string(bits.count())};
  result.push_back(JAWS_part_size_delimiter);
  result.append(ja3::hex::hexify_bitset(bits));
  return result;
}

struct JAWSParts {
  std::string ciphers_score;
  std::string extensions_score;
  std::string elliptic_curves_score;
};

JAWSParts
get_JAWS_parts(std::string_view JA3_string)
{
  TokenStream tokens{JA3_string, JA3_part_delimiter};
  tokens.skip();
  return JAWSParts{encode_score(anchor_order::cipher_suites, tokens.consume()),
                   encode_score(anchor_order::extensions, tokens.consume()),
                   encode_score(anchor_order::elliptic_curves, tokens.consume())};
}

std::string
join_JAWS_parts(JAWSParts const &parts, char delimiter)
{
  std::string result;
  result.append(parts.ciphers_score);
  result.push_back(delimiter);
  result.append(parts.extensions_score);
  result.push_back(delimiter);
  result.append(parts.elliptic_curves_score);
  return result;
}

} // end anonymous namespace

std::string
ja3::jaws_v1::score(std::string_view ja3_string)
{
  return join_JAWS_parts(get_JAWS_parts(ja3_string), JAWS_part_delimiter);
}

std::string
ja3::jaws_v1::fingerprint(ClientHelloSummary const &summary)
{
  return score(ja3::make_ja3_raw(summary, false));
}
