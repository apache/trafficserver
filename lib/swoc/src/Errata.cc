// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file

    Errata implementation.
 */

#include <iostream>
#include <sstream>
#include <algorithm>
#include <memory.h>
#include "swoc/Errata.h"
#include "swoc/bwf_ex.h"
#include "swoc/bwf_std.h"

using swoc::MemArena;
using std::string_view;
using namespace std::literals;
using namespace swoc::literals;

namespace swoc { inline namespace SWOC_VERSION_NS {
/** List of sinks for abandoned erratum.
 */
namespace {
std::vector<Errata::Sink::Handle> Sink_List;
}

string_view
Errata::Data::localize(string_view src) {
  auto span = _arena.alloc(src.size()).rebind<char>();
  memcpy(span.data(), src.data(), src.size());
  return {span.data(), span.size()};
}

/* ----------------------------------------------------------------------- */
// methods for Errata

Errata::Severity Errata::DEFAULT_SEVERITY(2);
Errata::Severity Errata::FAILURE_SEVERITY(2);
Errata::Severity Errata::FILTER_SEVERITY(0);

/// Default set of severity names.
std::array<swoc::TextView, 3> Severity_Names{
  {"Info", "Warning", "Error"}
};

swoc::MemSpan<TextView const> Errata::SEVERITY_NAMES{Severity_Names.data(), Severity_Names.size()};

Errata::~Errata() {
  this->sink();
}

Errata &
Errata::sink() {
  if (_data) {
    for (auto &f : Sink_List) {
      (*f)(*this);
    }
    this->clear();
  }
  return *this;
}

Errata &
Errata::note(code_type const &code) {
  return this->note("{}"_sv, code);
}

Errata &
Errata::note(code_type const &code, Severity severity) {
  return this->note(severity, "{}"_sv, code);
}

Errata::Data *
Errata::data() {
  if (!_data) {
    MemArena arena{512}; // POOMA value, seems reasonable.
    _data = arena.make<Data>(std::move(arena));
  }
  return _data;
}

Errata &
Errata::note_s(std::optional<Severity> severity, std::string_view text) {
  if (severity.has_value()) {
    this->update(*severity);
  }
  if (!severity.has_value() || *severity >= FILTER_SEVERITY) {
    auto span = this->alloc(text.size());
    memcpy(span, text);
    this->note_localized(TextView(span), severity);
  }
  return *this;
}

Errata &
Errata::note_localized(std::string_view const &text, std::optional<Severity> severity) {
  auto d  = this->data();
  auto *n = d->_arena.make<Annotation>(text, severity);
  d->_notes.append(n);
  return *this;
}

MemSpan<char>
Errata::alloc(size_t n) {
  return this->data()->_arena.alloc(n).rebind<char>();
}

Errata &
Errata::note(const self_type &that) {
  if (that._data) {
    auto d = this->data();
    if (that.has_severity()) {
      this->update(that.severity());
    }
    for (auto const &annotation : that) {
      d->_notes.append(d->_arena.make<Annotation>(d->localize(annotation._text), annotation._severity, annotation._level + 1));
    }
  }
  return *this;
}

auto
Errata::update(Severity severity) -> self_type & {
  if (!_data || !_data->_severity.has_value() || _data->_severity.value() < severity) {
    this->assign(severity);
  }
  return *this;
}

void
Errata::register_sink(Sink::Handle const &s) {
  Sink_List.push_back(s);
}

BufferWriter &
bwformat(BufferWriter &bw, bwf::Spec const &spec, Errata::Severity level) {
  if (level < Errata::SEVERITY_NAMES.size()) {
    bwformat(bw, spec, Errata::SEVERITY_NAMES[level]);
  } else {
    bwformat(bw, spec, level._raw);
  }
  return bw;
}

BufferWriter &
bwformat(BufferWriter &bw, bwf::Spec const &, Errata const &errata) {
  bwf::Format const code_fmt{"[{0:s} {0:d}] "};

  if (errata.has_severity()) {
    bw.print("{}{}", errata.severity(), errata.severity_glue_text());
  }

  if (errata.code()) {
    bw.print(code_fmt, errata.code());
  }

  bool trailing_p = false;
  auto glue       = errata.annotation_glue_text();
  auto a_s_glue   = errata.annotation_severity_glue_text();
  auto id_txt     = errata.indent_text();
  for (auto &note : errata) {
    if (note.text()) {
      bw.print("{}{}{}{}", swoc::bwf::If(trailing_p, "{}", glue), swoc::bwf::Pattern{int(note.level()), id_txt},
               swoc::bwf::If(note.has_severity(), "{}{}", note.severity(), a_s_glue), note.text());
      trailing_p = true;
    }
  }
  if (trailing_p && errata._data->_glue_final_p) {
    bw.print("{}", glue);
  }
  return bw;
}

std::ostream &
Errata::write(std::ostream &out) const {
  std::string tmp;
  tmp.reserve(1024);
  bwprint(tmp, "{}", *this);
  return out << tmp;
}

std::ostream &
operator<<(std::ostream &os, Errata const &err) {
  return err.write(os);
}

}} // namespace swoc::SWOC_VERSION_NS
