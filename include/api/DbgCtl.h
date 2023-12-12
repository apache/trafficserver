/** @file

  DbgCtl class header file.

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

#include "api/SourceLocation.h"
#include "api/ts_diag_levels.h"
#include "swoc/BufferWriter.h"
#include <atomic>
#include <utility>
#include <functional>

#include <ts/apidefs.h> // For TS_PRINTFLIKE

class DiagsConfigState;

enum DiagsShowLocation { SHOW_LOCATION_NONE = 0, SHOW_LOCATION_DEBUG, SHOW_LOCATION_ALL };

class DebugInterface
{
public:
  virtual ~DebugInterface()                            = default;
  virtual bool debug_tag_activated(const char *) const = 0;
  virtual bool get_override() const                    = 0;
  virtual void print_va(const char *debug_tag, DiagsLevel diags_level, const SourceLocation *loc, const char *format_string,
                        va_list ap) const              = 0;

  static DebugInterface *get_instance();
  static void set_instance(DebugInterface *);

  // Generate the default diagnostics format string for the given parameters.
  // @return The offset in the format string of the timestamp (in case the caller doesn't want to include that)
  static size_t generate_format_string(swoc::LocalBufferWriter<1024> &format_writer, const char *debug_tag, DiagsLevel diags_level,
                                       const SourceLocation *loc, DiagsShowLocation show_location, const char *format_string);
  static const char *level_name(DiagsLevel dl);
};

class DbgCtl
{
public:
  // Tag is a debug tag.  Debug output associated with this control will be output when debug output
  // is enabled globally, and the tag matches the configured debug tag regular expression.
  //
  DbgCtl(char const *tag) : _ptr{_new_reference(tag)} {}

  // As instance with no tag will always be off.
  //
  DbgCtl() : _ptr{&_No_tag_dummy()} {}

  ~DbgCtl()
  {
    if (_ptr != &_No_tag_dummy()) {
      _rm_reference();
    }
  }

  // No copying. Only moving from a tagged to a tagless instance allowed.
  //
  DbgCtl(DbgCtl const &)            = delete;
  DbgCtl &operator=(DbgCtl const &) = delete;
  DbgCtl(DbgCtl &&);
  DbgCtl &operator=(DbgCtl &&);

  // A shorthand.
  //
  void
  set(char const *tag)
  {
    *this = DbgCtl{tag};
  }

  bool
  tag_on() const
  {
    return _ptr->second;
  }

  char const *
  tag() const
  {
    return _ptr->first;
  }

  bool
  on() const
  {
    auto m{_config_mode.load(std::memory_order_relaxed)};
    if (!m) {
      return false;
    }
    if (!_ptr->second) {
      return false;
    }
    if (m & 1) {
      return true;
    }
    return (2 == m) && (_override_global_on());
  }

  static bool
  global_on()
  {
    auto m{_config_mode.load(std::memory_order_relaxed)};
    if (!m) {
      return false;
    }
    if (m & 1) {
      return true;
    }
    return (2 == m) && (_override_global_on());
  }

  // Call this when the compiled regex to enable tags may have changed.
  //
  static void update(const std::function<bool(const char *)> &f);

  // For use in DbgPrint() only.
  //
  static void print(char const *tag, char const *file, char const *function, int line, char const *fmt_str, ...)
    TS_PRINTFLIKE(5, 6);

private:
  using _TagData = std::pair<char const *const, bool>;

  _TagData const *_ptr;

  static const _TagData &
  _No_tag_dummy()
  {
    static DbgCtl::_TagData const ntd{nullptr, false};
    return ntd;
  }

  static const _TagData *_new_reference(char const *tag);

  static void _rm_reference();

  class _RegistryAccessor;

  static std::atomic<int> _config_mode;

  static bool _override_global_on();

  friend class DiagsConfigState;
};

// Always generates output when called.
//
#define DbgPrint(CTL, ...) (DbgCtl::print((CTL).tag(), __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__))

#define Dbg(CTL, ...)               \
  do {                              \
    if ((CTL).on()) {               \
      DbgPrint((CTL), __VA_ARGS__); \
    }                               \
  } while (false)

// Same as Dbg above, but this allows a positive override of the DbgCtl, if FLAG is true.
//
#define SpecificDbg(FLAG, CTL, ...)   \
  do {                                \
    if (DbgCtl::global_on()) {        \
      if ((FLAG) || ((CTL).on())) {   \
        DbgPrint((CTL), __VA_ARGS__); \
      }                               \
    }                                 \
  } while (false)
