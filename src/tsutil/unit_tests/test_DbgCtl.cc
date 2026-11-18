/**
  @file Test for DbgCtl.cc

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
#include <string_view>
#include <utility>

#include "tsutil/DbgCtl.h"
#include "swoc/BufferWriter.h"

#include <catch2/catch_test_macros.hpp>

// Dbg() runs in destructors and reaches generate_format_string, so it must
// stay noexcept.
static_assert(noexcept(DebugInterface::generate_format_string(std::declval<swoc::LocalBufferWriter<1024> &>(), nullptr, DL_Diag,
                                                              nullptr, SHOW_LOCATION_NONE, nullptr)));

namespace
{
// swoc would throw on or rewrite each brace form if it parsed the caller's
// format, so an exact copy in the output shows that it did not. The printf
// conversion checks that the caller's own conversions pass through too.
constexpr std::string_view BRACES = "open { close } hex {:x} index {0} name {name} printf %s";

// The thread name between the timestamp and the level is not controlled by
// the test, so the output is checked from the level onward.
std::string_view
after_level(std::string_view out, std::string_view level_and_tag)
{
  auto pos = out.find(level_and_tag);
  REQUIRE(pos != std::string_view::npos);
  return out.substr(pos + level_and_tag.size());
}
} // namespace

TEST_CASE("generate_format_string copies the caller's format unparsed", "[DbgCtl]")
{
  swoc::LocalBufferWriter<1024> w;
  std::string_view              tag;
  const char                   *debug_tag = nullptr;

  SECTION("with a debug tag")
  {
    debug_tag = "my_tag";
    tag       = " DIAG: (my_tag) ";
  }
  SECTION("without a debug tag")
  {
    tag = " DIAG: ";
  }

  std::string fmt{BRACES};
  size_t      offset = 0;
  REQUIRE_NOTHROW(offset = DebugInterface::generate_format_string(w, debug_tag, DL_Diag, nullptr, SHOW_LOCATION_NONE, fmt.c_str()));

  std::string_view out = w.view();
  REQUIRE_FALSE(w.error());
  // The returned offset lets a caller drop the "[timestamp] " prefix.
  REQUIRE(out.front() == '[');
  REQUIRE(offset > 2);
  REQUIRE(out.substr(offset - 2, 2) == "] ");

  // The output is meant to be passed on as a C string with a trailing newline.
  REQUIRE(after_level(out, tag) == fmt + "\n" + '\0');
}

TEST_CASE("generate_format_string does not double a caller's newline", "[DbgCtl]")
{
  swoc::LocalBufferWriter<1024> w;
  std::string                   fmt = std::string{BRACES} + "\n";

  DebugInterface::generate_format_string(w, nullptr, DL_Diag, nullptr, SHOW_LOCATION_NONE, fmt.c_str());

  REQUIRE(after_level(w.view(), " DIAG: ") == fmt + '\0');
}

TEST_CASE("generate_format_string truncates a long format and still terminates it", "[DbgCtl]")
{
  swoc::LocalBufferWriter<1024> w;
  std::string                   fmt;
  while (fmt.size() < 2000) {
    fmt += BRACES;
  }

  REQUIRE_NOTHROW(DebugInterface::generate_format_string(w, nullptr, DL_Diag, nullptr, SHOW_LOCATION_NONE, fmt.c_str()));

  // Two bytes are held back while formatting, so a truncated line still gets
  // its newline and NUL inside the buffer.
  std::string_view out = w.view();
  REQUIRE_FALSE(w.error());
  REQUIRE(out.size() == w.capacity());
  REQUIRE(out.substr(out.size() - 2) == std::string_view{"\n\0", 2});

  auto body = after_level(out, " DIAG: ");
  body.remove_suffix(2);
  REQUIRE_FALSE(body.empty());
  REQUIRE(body.size() < fmt.size());
  REQUIRE(body == std::string_view{fmt}.substr(0, body.size()));
}
