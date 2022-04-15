/** @file

  common.cc - Some common functions everyone needs

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

#include <cstring>
#include <openssl/ssl.h>

#include <ts/ts.h>
#include <ts/apidefs.h>

#include "common.h"

const unsigned char hex_chars[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

std::string
hex_str(std::string const &str)
{
  std::string hex_str;
  hex_str.reserve(str.size() * 2);
  for (unsigned long int i = 0; i < str.size(); ++i) {
    unsigned char c    = str.at(i);
    hex_str[i * 2]     = hex_chars[(c & 0xF0) >> 4];
    hex_str[i * 2 + 1] = hex_chars[(c & 0x0F)];
  }
  return hex_str;
}
