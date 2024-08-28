/*
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

#include <pthread.h>
#include <unistd.h>
#include <thread>

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

#include "tsutil/StringConvert.h"

#if CRIPTS_HAS_MAXMIND
#include <maxminddb.h>

MMDB_s              *gMaxMindDB    = nullptr;
const cripts::string maxMindDBPath = CRIPTS_MAXMIND_DB;
#endif

void
global_initialization()
{
  std::srand(std::time(nullptr) * getpid());

#if CRIPTS_HAS_MAXMIND
  gMaxMindDB = new MMDB_s;

  int status = MMDB_open(maxMindDBPath.c_str(), MMDB_MODE_MMAP, gMaxMindDB);
  if (MMDB_SUCCESS != status) {
    TSError("[Cripts] Cannot open %s - %s", maxMindDBPath.c_str(), MMDB_strerror(status));
    delete gMaxMindDB;
    gMaxMindDB = nullptr;
    return;
  }
#endif

  // Initialize various sub modules
  cripts::Plugin::Remap::Initialize();
}

integer
integer_helper(std::string_view sv)
{
  integer res    = INT64_MIN;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), res);

  if (ec == std::errc()) {
    return res;
  } else {
    return INT64_MIN;
  }
}

int
cripts::Random(int max)
{
  static std::random_device          r;
  static std::default_random_engine  e1(r());
  std::uniform_int_distribution<int> uniform_dist(1, max);

  return uniform_dist(e1);
}

namespace cripts::details
{

template <typename T>
std::vector<T>
Splitter(T input, char delim)
{
  std::vector<T> output;
  size_t         first = 0;

  while (first < input.size()) {
    const auto second = input.find_first_of(delim, first);

    if (first != second) {
      output.emplace_back(input.substr(first, second - first));
    }
    if (second == cripts::string_view::npos) {
      break;
    }
    first = second + 1;
  }

  return output; // RVO
}

} // namespace cripts::details

cripts::string
cripts::Hex(const cripts::string &str)
{
  return ts::hex(str);
}

cripts::string
cripts::Hex(cripts::string_view sv)
{
  return ts::hex(sv);
}

cripts::string
cripts::UnHex(const cripts::string &str)
{
  return ts::unhex(str);
}

cripts::string
cripts::UnHex(cripts::string_view sv)
{
  return ts::unhex(sv);
}

cripts::string::operator integer() const
{
  return swoc::svtoi(*this);
}

// This doesn't deal with upper/lower case
cripts::string::operator bool() const
{
  if (empty()) {
    return false;
  }

  if (size() == 1) {
    return *this != "0";
  }

  if (size() == 4) {
    return *this == "true";
  }

  if (size() == 2) {
    return (*this == "on" || *this == "no");
  }

  return false;
}

std::vector<cripts::string_view>
cripts::string::split(char delim) const &
{
  return details::Splitter<cripts::string_view>(*this, delim);
}

std::vector<cripts::string_view>
cripts::Splitter(cripts::string_view input, char delim)
{
  return details::Splitter<cripts::string_view>(input, delim);
}

bool
cripts::Control::Base::_get(cripts::Context *context) const
{
  return TSHttpTxnCntlGet(context->state.txnp, _ctrl);
}

void
cripts::Control::Base::_set(cripts::Context *context, bool value)
{
  TSHttpTxnCntlSet(context->state.txnp, _ctrl, value);
}

cripts::string_view
cripts::Versions::GetSV()
{
  if (_version.length() == 0) {
    const char *ver = TSTrafficServerVersionGet();

    _version = cripts::string_view(ver, strlen(ver)); // ToDo: Annoyingly, we have ambiquity on the operator=
  }

  return _version;
}

// Globals
cripts::Proxy    proxy;
cripts::Control  control;
cripts::Versions version;

std::string plugin_debug_tag;

// This is to allow us to have a global initialization routine
pthread_once_t init_once_control = PTHREAD_ONCE_INIT;
pthread_once_t debug_tag_control = PTHREAD_ONCE_INIT;
