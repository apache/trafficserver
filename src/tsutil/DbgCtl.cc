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

#include <ctime>
#include <mutex>
#include <map>
#include <cstring>
#include <atomic>
#include <cstdarg>
#include <cinttypes>

#include "tsutil/SourceLocation.h"

#include "tsutil/ts_diag_levels.h"
#include "tsutil/ts_bw_format.h"
#include "tsutil/DbgCtl.h"
#include "tsutil/Assert.h"

#include "tscore/ink_config.h"

using namespace swoc::literals;

DbgCtl::DbgCtl(DbgCtl &&src)
{
  _ptr     = src._ptr;
  src._ptr = &_No_tag_dummy();
}

DbgCtl &
DbgCtl::operator=(DbgCtl &&src)
{
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
    debug_assert(r != nullptr);
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
  DebugInterface *p = DebugInterface::get_instance();
  debug_assert(tag != nullptr);

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

  debug_assert(sz > 0);

  char *t = new char[sz + 1]; // Deleted by ~Registry().
  std::memcpy(t, tag, sz + 1);
  _TagData new_elem{t, p && p->debug_tag_activated(tag)};

  auto res = d.map.insert(new_elem);

  debug_assert(res.second);

  return &*res.first;
}

void
DbgCtl::_rm_reference()
{
  _RegistryAccessor ra;

  debug_assert(ra.registry_reference_count != 0);

  --ra.registry_reference_count;

  if (0 == ra.registry_reference_count) {
    ra.delete_registry();
  }
}

void
DbgCtl::update(const std::function<bool(const char *)> &f)
{
  _RegistryAccessor ra;

  if (!ra.registry_reference_count) {
    return;
  }

  auto &d{ra.data()};

  for (auto &i : d.map) {
    i.second = f(i.first);
  }
}

void
DbgCtl::print(char const *tag, char const *file, char const *function, int line, char const *fmt_str, ...)
{
  DebugInterface *p = DebugInterface::get_instance();
  SourceLocation src_loc{file, function, line};
  if (p) {
    va_list args;
    va_start(args, fmt_str);
    p->print_va(tag, DL_Diag, &src_loc, fmt_str, args);
    va_end(args);
  } else {
    swoc::LocalBufferWriter<1024> format_writer;
    DebugInterface::generate_format_string(format_writer, tag, DL_Diag, &src_loc, SHOW_LOCATION_DEBUG, fmt_str);
    va_list args;
    va_start(args, fmt_str);
    vprintf(format_writer.data(), args);
    va_end(args);
  }
}

std::atomic<int> DbgCtl::_config_mode{0};

bool
DbgCtl::_override_global_on()
{
  DebugInterface *p = DebugInterface::get_instance();
  if (p) {
    return p->get_override();
  } else {
    return false;
  }
}

namespace
{
static DebugInterface *di_inst;

bool
location(const SourceLocation *loc, DiagsShowLocation show, DiagsLevel level)
{
  if (loc && loc->valid()) {
    switch (show) {
    case SHOW_LOCATION_ALL:
      return true;
    case SHOW_LOCATION_DEBUG:
      return level <= DL_Debug;
    default:
      return false;
    }
  }

  return false;
}

//////////////////////////////////////////////////////////////////////////////
//
//      const char *Diags::level_name(DiagsLevel dl)
//
//      This routine returns a string name corresponding to the error
//      level <dl>, suitable for us as an output log entry prefix.
//
//////////////////////////////////////////////////////////////////////////////

} // namespace

DebugInterface *
DebugInterface::get_instance()
{
  return di_inst;
}

void
DebugInterface::set_instance(DebugInterface *i)
{
  di_inst = i;
  DbgCtl::update([&](const char *t) { return i->debug_tag_activated(t); });
}

const char *
DebugInterface::level_name(DiagsLevel dl)
{
  switch (dl) {
  case DL_Diag:
    return ("DIAG");
  case DL_Debug:
    return ("DEBUG");
  case DL_Status:
    return ("STATUS");
  case DL_Note:
    return ("NOTE");
  case DL_Warning:
    return ("WARNING");
  case DL_Error:
    return ("ERROR");
  case DL_Fatal:
    return ("FATAL");
  case DL_Alert:
    return ("ALERT");
  case DL_Emergency:
    return ("EMERGENCY");
  default:
    return ("DIAG");
  }
}

namespace
{

struct DiagTimestamp {
  std::chrono::time_point<std::chrono::system_clock> ts = std::chrono::system_clock::now();
};

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, DiagTimestamp const &ts)
{
  auto epoch = std::chrono::system_clock::to_time_t(ts.ts);
  swoc::LocalBufferWriter<48> lw;

  ctime_r(&epoch, lw.aux_data());
  lw.commit(19); // keep only leading text.
  lw.print(".{:03}", std::chrono::time_point_cast<std::chrono::milliseconds>(ts.ts).time_since_epoch().count() % 1000);
  w.write(lw.view().substr(4));

  return w;
}

struct DiagThreadname {
  char name[32];

  DiagThreadname()
  {
#if defined(HAVE_PTHREAD_GETNAME_NP)
    pthread_getname_np(pthread_self(), name, sizeof(name));
#elif defined(HAVE_PTHREAD_GET_NAME_NP)
    pthread_get_name_np(pthread_self(), name, sizeof(name));
#elif defined(HAVE_PRCTL) && defined(PR_GET_NAME)
    prctl(PR_GET_NAME, name, 0, 0, 0);
#else
    snprintf(name, sizeof(name), "0x%" PRIx64, (uint64_t)pthread_self());
#endif
  }
};

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, DiagThreadname const &n)
{
  bwformat(w, spec, std::string_view{n.name});
  return w;
}

} // namespace

size_t
DebugInterface::generate_format_string(swoc::LocalBufferWriter<1024> &format_writer, const char *debug_tag, DiagsLevel diags_level,
                                       const SourceLocation *loc, DiagsShowLocation show_location, const char *format_string)
{
  // Save room for optional newline and terminating NUL bytes.
  format_writer.restrict(2);

  format_writer.print("[{}] ", DiagTimestamp{});
  auto timestamp_offset = format_writer.size();

  format_writer.print("{} {}: ", DiagThreadname{}, level_name(diags_level));

  if (location(loc, show_location, diags_level)) {
    format_writer.print("<{}> ", *loc);
  }

  if (debug_tag) {
    format_writer.print("({}) ", debug_tag);
  }

  format_writer.print("{}", format_string);

  format_writer.restore(2);                  // restore the space for required termination.
  if (format_writer.view().back() != '\n') { // safe because always some chars in the buffer.
    format_writer.write('\n');
  }
  format_writer.write('\0');

  return timestamp_offset;
}
