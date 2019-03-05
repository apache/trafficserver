/** @file

    Errata implementation.

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#include <iostream>
#include <sstream>
#include <algorithm>
#include <memory.h>
#include "swoc/Errata.h"
#include "swoc/bwf_ex.h"

using swoc::MemArena;
using std::string_view;

namespace swoc
{
/** List of sinks for abandoned erratum.
 */
namespace
{
  std::vector<Errata::Sink::Handle> Sink_List;
}

std::string_view const Errata::DEFAULT_GLUE{"\n", 1};

/** This is the implementation class for Errata.

    It holds the actual messages and is treated as a passive data object with nice constructors.
*/
string_view
Errata::Data::localize(string_view src)
{
  auto span = _arena.alloc(src.size());
  memcpy(span.data(), src.data(), src.size());
  return span;
}

/* ----------------------------------------------------------------------- */
// methods for Errata

Errata::~Errata()
{
  this->release();
}

void
Errata::release()
{
  if (_data) {
    if (--(_data->_ref_count) == 0) {
      if (!_data->empty()) {
        for (auto &f : Sink_List) {
          (*f)(*this);
        }
      }
      _data->~Data();
    }
    _data = nullptr;
  }
}

const Errata::Data *
Errata::data()
{
  if (!_data) {
    MemArena arena{512};
    _data = arena.make<Data>(std::move(arena));
    ++(_data->_ref_count);
  }
  return _data;
}

Errata::Data *
Errata::writeable_data()
{
  if (!_data) {
    this->data(); // force data existence, must be unique.
  } else if (_data->_ref_count > 1) {
    // Pondering this, there's really no good use case for shared write access to an Errata.
    // The shared_ptr is used only to make function returns efficient. Explicit copying is
    // easy using @c note.
    throw std::runtime_error("Shared write to Errata");
  };
  return _data;
}

Errata::iterator
Errata::begin()
{
  return _data ? _data->_notes.begin() : iterator();
}

Errata::const_iterator
Errata::begin() const
{
  return _data ? _data->_notes.begin() : const_iterator();
}

Errata::iterator
Errata::end()
{
  return _data ? _data->_notes.end() : iterator();
}

Errata::const_iterator
Errata::end() const
{
  return _data ? _data->_notes.end() : const_iterator();
}

Severity
Errata::severity() const
{
  return _data ? _data->_severity : DEFAULT_SEVERITY;
}

Errata &
Errata::note(Severity level, std::string_view text)
{
  auto d        = this->writeable_data();
  Annotation *n = d->_arena.make<Annotation>(level, d->localize(text));
  d->_notes.prepend(n);
  _data->_severity = std::max(_data->_severity, level);
  return *this;
}

Errata &
Errata::note_localized(Severity level, std::string_view const &text)
{
  auto d        = this->writeable_data();
  Annotation *n = d->_arena.make<Annotation>(level, text);
  n->_level     = d->_level;
  d->_notes.prepend(n);
  _data->_severity = std::max(_data->_severity, level);
  return *this;
}

MemSpan<char>
Errata::alloc(size_t n)
{
  return this->writeable_data()->_arena.alloc(n).rebind<char>();
}

Errata &
Errata::note(const self_type &that)
{
  for (auto const &m : that) {
    this->note(m._severity, m._text);
  }
  return *this;
}

Errata &
Errata::clear()
{
  if (_data) {
    _data->_notes.clear(); // Prevent sink processing.
    this->release();
  }
  return *this;
}

void
Errata::register_sink(Sink::Handle const &s)
{
  Sink_List.push_back(s);
}

std::ostream &
Errata::write(std::ostream &out) const
{
  string_view lead;
  for (auto &m : *this) {
    out << lead << " [" << static_cast<int>(m._severity) << "]: " << m._text << std::endl;
    if (0 == lead.size()) {
      lead = "  "_sv;
    }
  }
  return out;
}

BufferWriter &
bwformat(BufferWriter &bw, bwf::Spec const &spec, Errata::Severity level)
{
  static constexpr std::string_view name[] = {"DIAG", "DEBUG", "INFO", "NOTE", "WARNING", "ERROR", "FATAL", "ALERT", "EMERGENCY"};
  return bwformat(bw, spec, name[static_cast<int>(level)]);
}

BufferWriter &
bwformat(BufferWriter &bw, bwf::Spec const &spec, Errata const &errata)
{
  for (auto &m : errata) {
    bw.print("{}[{}] {}\n", swoc::bwf::Pattern{int(m.level()), "  "}, m.severity(), m.text());
  }
  return bw;
}

std::ostream &
operator<<(std::ostream &os, Errata const &err)
{
  return err.write(os);
}

} // namespace swoc
