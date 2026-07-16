/** @file

  Configuration helpers for the redo_cache_lookup plugin.

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

#pragma once

#include <getopt.h>
#include <optional>
#include <string>

namespace redo_cache_lookup
{
/** Parse the configured fallback URL from plugin arguments.
 *
 * @param[in] argc The number of plugin argument entries in @a argv.
 * @param[in] argv The plugin arguments supplied from @c plugin.config.
 * @return The configured fallback URL, or @c std::nullopt if no fallback URL
 *   is configured.
 */
inline std::optional<std::string>
parse_fallback_url(int argc, const char *argv[])
{
  std::optional<std::string> fallback;

  static const struct option longopts[] = {
    {"fallback", required_argument, nullptr, 'f'},
    {nullptr,    0,                 nullptr, 0  },
  };

#if (!defined(kfreebsd) && defined(freebsd)) || defined(darwin)
  optreset = 1;
#endif
#if defined(__GLIBC__)
  optind = 0;
#else
  optind = 1;
#endif
  opterr = 0;
  optarg = nullptr;

  int opt = 0;

  while (opt >= 0) {
    opt = getopt_long(argc, const_cast<char *const *>(argv), "f:", longopts, nullptr);
    switch (opt) {
    case 'f':
      fallback = optarg;
      break;
    case -1:
    case '?':
      break;
    default:
      return std::nullopt;
    }
  }

  return fallback;
}
} // namespace redo_cache_lookup
