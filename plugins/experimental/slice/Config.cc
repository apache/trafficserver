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

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <map>
#include <string>

int64_t
Config::bytesFrom(std::string const &valstr)
{
  char const *const nptr = valstr.c_str();
  char *endptr           = nullptr;
  int64_t blockbytes     = strtoll(nptr, &endptr, 10);

  if (nullptr != endptr && nptr < endptr) {
    size_t const dist = endptr - nptr;
    if (dist < valstr.size() && 0 <= blockbytes) {
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
Config::fromArgs(int const argc, char const *const argv[], char *const errbuf, int const errbuf_size)
{
#if !defined(SLICE_UNIT_TEST)
  DEBUG_LOG("Number of arguments: %d", argc);
  for (int index = 0; index < argc; ++index) {
    DEBUG_LOG("args[%d] = %s", index, argv[index]);
  }
#endif

  std::map<std::string, std::string> keyvals;

  static std::string const bbstr(blockbytesstr);
  static std::string const bostr(bytesoverstr);

  // collect all args
  for (int index = 0; index < argc; ++index) {
    std::string const argstr = argv[index];

    std::size_t const spos = argstr.find_first_of(":");
    if (spos != std::string::npos) {
      std::string key = argstr.substr(0, spos);
      std::string val = argstr.substr(spos + 1);

      if (!key.empty()) {
        std::for_each(key.begin(), key.end(), [](char &ch) { ch = tolower(ch); });

        // blockbytes and bytesover collide
        if (bbstr == key) {
          keyvals.erase(bostr);
        } else if (bostr == key) {
          keyvals.erase(bbstr);
        }

        keyvals[std::move(key)] = std::move(val);
      }
    }
  }

  std::map<std::string, std::string>::const_iterator itfind;

  // blockbytes checked range string
  itfind = keyvals.find(bbstr);
  if (keyvals.end() != itfind) {
    std::string val = itfind->second;
    if (!val.empty()) {
      int64_t const blockbytes = bytesFrom(val);

      if (blockbytes < blockbytesmin || blockbytesmax < blockbytes) {
#if !defined(SLICE_UNIT_TEST)
        DEBUG_LOG("Block Bytes %" PRId64 " outside checked limits %" PRId64 "-%" PRId64, blockbytes, blockbytesmin, blockbytesmax);
        DEBUG_LOG("Block Bytes kept at %" PRId64, m_blockbytes);
#endif
      } else {
#if !defined(SLICE_UNIT_TEST)
        DEBUG_LOG("Block Bytes set to %" PRId64, blockbytes);
#endif
        m_blockbytes = blockbytes;
      }
    }

    keyvals.erase(itfind);
  }

  // bytesover unchecked range string
  itfind = keyvals.find(bostr);
  if (keyvals.end() != itfind) {
    std::string val = itfind->second;
    if (!val.empty()) {
      int64_t const bytesover = bytesFrom(val);

      if (bytesover <= 0) {
#if !defined(SLICE_UNIT_TEST)
        DEBUG_LOG("Bytes Over %" PRId64 " <= 0", bytesover);
        DEBUG_LOG("Block Bytes kept at %" PRId64, m_blockbytes);
#endif
      } else {
#if !defined(SLICE_UNIT_TEST)
        DEBUG_LOG("Block Bytes set to %" PRId64, bytesover);
#endif
        m_blockbytes = bytesover;
      }
    }
    keyvals.erase(itfind);
  }

  for (std::map<std::string, std::string>::const_iterator itkv(keyvals.cbegin()); keyvals.cend() != itkv; ++itkv) {
#if !defined(SLICE_UNIT_TEST)
    ERROR_LOG("Unhandled pparam %s", itkv->first.c_str());
#endif
  }

  return true;
}
