/** @file

  Implementation file for DbgCtl class.

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

#include <mutex>
#include <atomic>
#include <set>
#include <cstring>

#include <tscore/ink_assert.h>
#include <tscore/Diags.h>

// The resistry of fast debug controllers has a ugly implementation to handle the whole-program initialization
// order problem with C++.
//
class DbgCtl::_RegistryAccessor
{
private:
  struct TagCmp {
    bool
    operator()(TSDbgCtl const &a, TSDbgCtl const &b) const
    {
      return std::strcmp(a.tag, b.tag) < 0;
    }
  };

public:
  _RegistryAccessor() : _lg(data().mtx) {}

  using Set = std::set<TSDbgCtl, TagCmp>;

  struct Data {
    std::mutex mtx;
    Set set;

    ~Data()
    {
      for (auto &ctl : set) {
        delete[] ctl.tag;
      }
    }
  };

  static Data &
  data()
  {
    static Data d;
    return d;
  }

private:
  std::lock_guard<std::mutex> _lg;
};

TSDbgCtl const *
DbgCtl::_get_ptr(char const *tag)
{
  ink_assert(tag != nullptr);

  TSDbgCtl ctl;

  ctl.tag = tag;

  _RegistryAccessor ra;

  auto &d{ra.data()};

  if (auto it = d.set.find(ctl); it != d.set.end()) {
    return &*it;
  }

  auto sz = std::strlen(tag);

  ink_assert(sz > 0);

  {
    char *t = new char[sz + 1]; // Deleted by ~Data().
    std::memcpy(t, tag, sz + 1);
    ctl.tag = t;
  }
  ctl.on = diags() && diags()->tag_activated(tag, DiagsTagType_Debug);

  auto res = d.set.insert(ctl);

  ink_assert(res.second);

  return &*res.first;
}

void
DbgCtl::update()
{
  ink_release_assert(diags() != nullptr);

  _RegistryAccessor ra;

  auto &d{ra.data()};

  for (auto &i : d.set) {
    const_cast<char volatile &>(i.on) = diags()->tag_activated(i.tag, DiagsTagType_Debug);
  }
}
