/** @file

  Provides class which provides a null terminated copy of an std::string_view.

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

#include <cstring>
#include <string_view>

namespace ts
{
// Class to provide a null terminated copy of as std::string_view, so:
// TSSomething("%.*s\n", static_cast<int>(sv.size()), sv.data());
// becomes:
// TSSomething("%s\n", ts::nt{sv}.v());
//
class nt
{
public:
  nt(std::string_view sv)
  {
    if (sv.size() >= sizeof(_lbuf)) {
      _hbuf = new char[sv.size() + 1];
      _nts  = _hbuf;
    } else {
      _nts = _lbuf;
    }
    std::memcpy(_nts, sv.data(), sv.size());
    _nts[sv.size()] = '\0';
  }

  // Returns the null terminated string.
  //
  char const *
  v() const
  {
    return _nts;
  }

  ~nt() { delete[] _hbuf; }

  // no copy/move
  nt(nt const &) = delete;
  nt &operator=(nt const &) = delete;

private:
  char *_hbuf{nullptr}, *_nts;
  char _lbuf[256];
};

} // end namespace ts
