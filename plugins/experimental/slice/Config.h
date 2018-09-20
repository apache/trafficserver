/** @file
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

#pragma once

#include "slice.h"

#include <string>

// Data Structures and Classes
struct Config {
  static int64_t const blockbytesmin     = 1024 * 256;       // 256KB
  static int64_t const blockbytesmax     = 1024 * 1024 * 32; // 32MB
  static int64_t const blockbytesdefault = 1024 * 1024;      // 1MB

  static constexpr char const *const blockbytesstr = "blockbytes";
  static constexpr char const *const bytesoverstr  = "bytesover";

  int64_t m_blockbytes{blockbytesdefault};

  // Last one wins
  bool fromArgs(int const argc, char const *const argv[], char *const errbuf, int const errbuf_size);

  static int64_t bytesFrom(std::string const &valstr);
};
