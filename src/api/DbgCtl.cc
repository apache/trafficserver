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
#include <map>
#include <cstring>
#include <atomic>
#include <cstdarg>

#include <tscore/ink_assert.h>
#include <tscore/Diags.h>

DbgCtl::DbgCtl(DbgCtl &&src)
{
  ink_release_assert(src._ptr != &_No_tag_dummy());

  _ptr     = src._ptr;
  src._ptr = &_No_tag_dummy();
}

DbgCtl &
DbgCtl::operator=(DbgCtl &&src)
{
  ink_release_assert(&_No_tag_dummy() == _ptr);

  new (this) DbgCtl{std::move(src)};

  return *this;
}

// The resistry of fast debug controllers has a ugly implementation to handle the whole-program initialization
// and destruction order problem with C++.
//
class DbgCtl::_RegistryAccessor
{
private:
  struct TagCmp {
    bool
    operator()(char const *a, char const *b) const
    {
      return std::strcmp(a, b) < 0;
    }
  };

public:
  using Map = std::map<char const *, bool, TagCmp>;

  class Registry
  {
  public:
    Map map;

  private:
    Registry() = default;

    // Mutex must be locked before this is called.
    //
    ~Registry()
    {
      for (auto &elem : map) {
        delete[] elem.first;
      }
      _mtx.unlock();
    }

    std::mutex _mtx;

    friend class DbgCtl::_RegistryAccessor;
  };

  _RegistryAccessor()
  {
    if (!_registry_instance) {
      Registry *expected{nullptr};
      Registry *r{new Registry};
      if (!_registry_instance.compare_exchange_strong(expected, r)) {
        r->_mtx.lock();
        delete r;
      }
    }
    _registry_instance.load()->_mtx.lock();
    _mtx_is_locked = true;
  }

  ~_RegistryAccessor()
  {
    if (_mtx_is_locked) {
      _registry_instance.load()->_mtx.unlock();
    }
  }

  // This is not static so it can't be called with the registry mutex is unlocked.  It should not be called
  // after registry is deleted.
  //
  Registry &
  data()
  {
    return *_registry_instance;
  }

  void
  delete_registry()
  {
    auto r = _registry_instance.load();
    ink_assert(r != nullptr);
    _registry_instance = nullptr;
    delete r;
    _mtx_is_locked = false;
  }

  // Reference count of references to Registry.
  //
  inline static std::atomic<unsigned> registry_reference_count{0};

private:
  bool _mtx_is_locked{false};

  inline static std::atomic<Registry *> _registry_instance{nullptr};
};

DbgCtl::_TagData const *
DbgCtl::_new_reference(char const *tag)
{
  ink_assert(tag != nullptr);

  // DbgCtl instances may be declared as static objects in the destructors of objects not destoyed till program exit.
  // So, we must handle the case where the construction of such instances of DbgCtl overlaps with the destruction of
  // other instances of DbgCtl.  That is why it is important to make sure the reference count is non-zero before
  // constructing _RegistryAccessor.  The _RegistryAccessor constructor is thereby able to assume that, if it creates
  // the Registry, the new Registry will not be destroyed before the mutex in the new Registry is locked.

  ++_RegistryAccessor::registry_reference_count;

  _RegistryAccessor ra;

  auto &d{ra.data()};

  if (auto it = d.map.find(tag); it != d.map.end()) {
    return &*it;
  }

  auto sz = std::strlen(tag);

  ink_assert(sz > 0);

  char *t = new char[sz + 1]; // Deleted by ~Registry().
  std::memcpy(t, tag, sz + 1);
  _TagData new_elem{t, diags() && diags()->tag_activated(tag, DiagsTagType_Debug)};

  auto res = d.map.insert(new_elem);

  ink_assert(res.second);

  return &*res.first;
}

void
DbgCtl::_rm_reference()
{
  _RegistryAccessor ra;

  ink_assert(ra.registry_reference_count != 0);

  --ra.registry_reference_count;

  if (0 == ra.registry_reference_count) {
    ra.delete_registry();
  }
}

void
DbgCtl::update()
{
  ink_release_assert(diags() != nullptr);

  _RegistryAccessor ra;

  if (!ra.registry_reference_count) {
    return;
  }

  auto &d{ra.data()};

  for (auto &i : d.map) {
    i.second = diags()->tag_activated(i.first, DiagsTagType_Debug);
  }
}

void
DbgCtl::print(char const *tag, char const *file, char const *function, int line, char const *fmt_str, ...)
{
  SourceLocation src_loc{file, function, line};
  va_list args;
  va_start(args, fmt_str);
  diags()->print_va(tag, DL_Diag, &src_loc, fmt_str, args);
  va_end(args);
}

std::atomic<int> DbgCtl::_config_mode{0};

bool
DbgCtl::_override_global_on()
{
  return diags()->get_override();
}
