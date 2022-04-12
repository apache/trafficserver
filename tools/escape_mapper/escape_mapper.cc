/** @file

  A brief file description

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

#include <iomanip>
#include <iostream>
#include <string_view>

void
add_mapping(unsigned char *table, char c)
{
  int i = c / 8;
  int x = 7 - c % 8;
  int r = 1 << x;

  table[i] = table[i] | r;
}

int
main(int argc, char *argv[])
{
  // only support a single arg that contains all the chars we wish to escapify
  if (argc > 1 && argc != 2) {
    std::cerr << "Provide a single argument with a list of characters to add to the default encoding table." << std::endl;
    return (1);
  }

  // default characters supported by the codes_to_escape table found in LogUtils.cc
  unsigned char to_escape[16] = {
    ' ', '"', '#', '%', '<', '>', '[', ']', '\\', '^', '`', '{', '|', '}', '~', 0x7F,
  };

  unsigned char escape_codes[32] = {0};

  // indexes 0-3 are marked as "control"
  for (int i = 0; i < 4; i++) {
    escape_codes[i] = 0xFF;
  }

  // add_mapping performs a logical or on the entries, so the above 0xFF values
  // will persist.
  for (auto char_to_escape : to_escape) {
    add_mapping(&escape_codes[0], char_to_escape);
  }

  // add the chars specified in argv
  if (argc > 1) {
    std::string_view escape_characters{argv[1]};
    for (auto const char_to_escape : escape_characters) {
      std::cout << "Adding '" << char_to_escape << "' to escape mapping table." << std::endl;
      add_mapping(&escape_codes[0], char_to_escape);
    }
    std::cout << std::endl;
  }

  std::string_view qualification{((argc > 1) ? "New" : "Default")};
  std::cout << qualification << " Escape Mapping Table:" << std::endl;

  for (unsigned long i = 0; i < sizeof(escape_codes) / sizeof(escape_codes[0]); i++) {
    std::cout << std::dec << std::setfill(' ') << std::setw(4) << i << ": 0x";
    auto const escape_code = static_cast<int>(escape_codes[i]);
    std::cout << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << escape_code << std::endl;
  }

  return 0;
}
