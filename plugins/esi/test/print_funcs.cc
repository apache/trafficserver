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

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <map>

#include <ts/ts.h>

static const int LINE_SIZE = 1024 * 1024;

namespace
{
bool fakeDebugLogEnabled;
}

std::string gFakeDebugLog;

void
enableFakeDebugLog()
{
  fakeDebugLogEnabled = true;
  gFakeDebugLog.assign("");
}

void
DbgCtl::print(const char *tag, const char * /* file */, const char * /* function */, int /* line */, const char *fmt, ...)
{
  char buf[LINE_SIZE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, LINE_SIZE, fmt, ap);
  printf("Debug (%s): %s\n", tag, buf);
  va_end(ap);
  if (fakeDebugLogEnabled) {
    gFakeDebugLog.append(buf);
  }
}

class DbgCtl::_RegistryAccessor
{
public:
  // No mutex protection, assuming unit test is single threaded.
  //
  static std::map<char const *, bool> &
  registry()
  {
    static std::map<char const *, bool> r;
    return r;
  }
  static inline int ref_count{0};
};

std::atomic<int> DbgCtl::_config_mode{1};

DbgCtl::_TagData const *
DbgCtl::_new_reference(char const *tag)
{
  ++_RegistryAccessor::ref_count;

  auto it{_RegistryAccessor::registry().find(tag)};
  if (it == _RegistryAccessor::registry().end()) {
    char *s = new char[std::strlen(tag) + 1];
    std::strcpy(s, tag);
    auto r{_RegistryAccessor::registry().emplace(s, true)}; // Tag is always enabled.
    it = r.first;
  }
  return &(*it);
}

void
DbgCtl::_rm_reference()
{
  if (!--_RegistryAccessor::ref_count) {
    for (auto &elem : _RegistryAccessor::registry()) {
      delete[] elem.first;
    }
  }
}

bool
DbgCtl::_override_global_on()
{
  return false;
}

void
TSError(const char *fmt, ...)
{
  char buf[LINE_SIZE];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, LINE_SIZE, fmt, ap);
  printf("Error: %s\n", buf);
  va_end(ap);
}
