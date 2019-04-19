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

#include "Config.h"

#include <cctype>
#include <cinttypes>
#include <cstdlib>
#include <getopt.h>
#include <string_view>

int64_t
Config::bytesFrom(char const *const valstr)
{
  char *endptr       = nullptr;
  int64_t blockbytes = strtoll(valstr, &endptr, 10);

  if (nullptr != endptr && valstr < endptr) {
    size_t const dist = endptr - valstr;
    if (dist < strlen(valstr) && 0 <= blockbytes) {
      switch (tolower(*endptr)) {
      case 'g':
        blockbytes *= ((int64_t)1024 * (int64_t)1024 * (int64_t)1024);
        break;
      case 'm':
        blockbytes *= ((int64_t)1024 * (int64_t)1024);
        break;
      case 'k':
        blockbytes *= (int64_t)1024;
        break;
      default:
        break;
      }
    }
  }

  if (blockbytes < 0) {
    blockbytes = 0;
  }

  return blockbytes;
}

bool
Config::fromArgs(int const argc, char const *const argv[])
{
  DEBUG_LOG("Number of arguments: %d", argc);
  for (int index = 0; index < argc; ++index) {
    DEBUG_LOG("args[%d] = %s", index, argv[index]);
  }

  // current "best" blockbytes from configuration
  int64_t blockbytes = 0;

  // backwards compat: look for blockbytes
  for (int index = 0; index < argc; ++index) {
    std::string_view const argstr = argv[index];

    std::size_t const spos = argstr.find_first_of(':');
    if (spos != std::string_view::npos) {
      std::string_view const key = argstr.substr(0, spos);
      std::string_view const val = argstr.substr(spos + 1);

      if (!key.empty() && !val.empty()) {
        char const *const valstr = val.data(); // inherits argv's null
        int64_t const bytesread  = bytesFrom(valstr);

        if (blockbytesmin <= bytesread && bytesread <= blockbytesmax) {
          DEBUG_LOG("Found deprecated blockbytes %" PRId64, bytesread);
          blockbytes = bytesread;
        }
      }
    }
  }

  // standard parsing
  constexpr const struct option longopts[] = {
    {const_cast<char *>("blockbytes"), required_argument, nullptr, 'b'},
    {const_cast<char *>("bytesover"), required_argument, nullptr, 'o'},
    {const_cast<char *>("disable-errorlog"), no_argument, nullptr, 'd'},
    {nullptr, 0, nullptr, 0},
  };

  // getopt assumes args start at '1' so this hack is needed
  char *const *argvp = ((char *const *)argv - 1);

  for (;;) {
    int const opt = getopt_long(argc + 1, argvp, "b:", longopts, nullptr);
    if (-1 == opt) {
      break;
    }

    DEBUG_LOG("processing '%c' %s", (char)opt, argvp[optind - 1]);

    switch (opt) {
    case 'b': {
      int64_t const bytesread = bytesFrom(optarg);
      if (blockbytesmin <= bytesread && bytesread <= blockbytesmax) {
        DEBUG_LOG("Using blockbytes %" PRId64, bytesread);
        blockbytes = bytesread;
      } else {
        ERROR_LOG("Invalid blockbytes: %s", optarg);
      }
    } break;
    case 'o':
      if (0 == blockbytes) {
        int64_t const bytesread = bytesFrom(optarg);
        if (0 < bytesread) {
          DEBUG_LOG("Using bytesover %" PRId64, bytesread);
          blockbytes = bytesread;
        } else {
          ERROR_LOG("Invalid bytesover: %s", optarg);
        }
      } else {
        DEBUG_LOG("Skipping bytesover in favor of blockbytes");
      }
      break;
    case 'd':
      DEBUG_LOG("Disabling block stitching error logs");
      m_disable_errorlog = true;
      break;
    default:
      break;
    }
  }

  if (0 < blockbytes) {
    DEBUG_LOG("Using configured blockbytes %" PRId64, blockbytes);
    m_blockbytes = blockbytes;
  } else {
    DEBUG_LOG("Using default blockbytes %" PRId64, m_blockbytes);
  }

  return true;
}
