/** @file
    Errata implementation.

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

# include "Errata.h"
# include <iostream>
# include <sstream>
# include <iomanip>
# include <algorithm>
# include <memory.h>

namespace ts {

/** List of sinks for abandoned erratum.
 */
namespace {
  std::deque<Errata::Sink::Handle> Sink_List;
}

std::string const Errata::DEFAULT_GLUE("\n");
Errata::Message const Errata::NIL_MESSAGE;
Errata::Code Errata::Message::Default_Code = 0;
Errata::Message::SuccessTest const Errata::Message::DEFAULT_SUCCESS_TEST =
  &Errata::Message::isCodeZero;
Errata::Message::SuccessTest Errata::Message::Success_Test =
  Errata::Message::DEFAULT_SUCCESS_TEST;

bool
Errata::Message::isCodeZero(Message const& msg) {
  return msg.m_code == 0;
}

void
Errata::Data::push(Message const& msg) {
  m_items.push_back(msg);
}

void
Errata::Data::push(Message && msg) {
  m_items.push_back(std::move(msg));
}

Errata::Message const&
Errata::Data::top() const {
  return m_items.size() ? m_items.back() : NIL_MESSAGE ;
}

inline Errata::Errata(ImpPtr const& ptr)
  : m_data(ptr) {
}

Errata::Data::~Data() {
  if (m_log_on_delete) {
    Errata tmp(ImpPtr(this)); // because client API requires a wrapper.
    for ( auto& f : Sink_List ) { (*f)(tmp); }
    tmp.m_data.release(); // don't delete this again.
  }
}

Errata::Errata(self const& that)
  : m_data(that.m_data) {
}

Errata::Errata(self && that)
  : m_data(that.m_data) {
}

Errata::Errata(std::string const& text) {
  this->push(text);
}

Errata::Errata(Id id, std::string const& text) {
  this->push(id, text);
}

Errata::~Errata() {
}

/*  This forces the errata to have a data object that only it references.
    If we're sharing the data, clone. If there's no data, allocate.
    This is used just before a write operation to have copy on write semantics.
 */
Errata::Data*
Errata::pre_write() {
  if (m_data) {
    if (m_data.use_count() > 1) {
      m_data.reset(new Data(*m_data)); // clone current data
    }
  } else { // create new data
    m_data.reset(new Data);
  }
  return m_data.get();
}

// Just create an instance if needed.
Errata::Data const*
Errata::instance() {
  if (!m_data) { m_data.reset(new Data);
}
  return m_data.get();
}

Errata&
Errata::push(Message const& msg) {
  this->pre_write()->push(msg);
  return *this;
}

Errata&
Errata::push(Message && msg) {
  this->pre_write()->push(std::move(msg));
  return *this;
}

Errata&
Errata::operator=(self const& that) {
  m_data = that.m_data;
  return *this;
}

Errata&
Errata::operator = (Message const& msg) {
  // Avoid copy on write in the case where we discard.
  if (!m_data || m_data.use_count() > 1) {
    this->clear();
    this->push(msg);
  } else {
    m_data->m_items.clear();
    m_data->push(msg);
  }
  return *this;
}

Errata&
Errata::operator = (self && that) {
  m_data = that.m_data;
  return *this;
}

Errata&
Errata::pull(self& that) {
  if (that.m_data) {
    this->pre_write();
    m_data->m_items.insert(
      m_data->m_items.end(),
      that.m_data->m_items.begin(),
      that.m_data->m_items.end()
    );
    that.m_data->m_items.clear();
  }
  return *this;
}

void
Errata::pop() {
  if (m_data && m_data->size()) {
    this->pre_write()->m_items.pop_front();
  }
  return;
}

void
Errata::clear() {
  m_data.reset(nullptr);
}

/*  We want to allow iteration on empty / nil containers because that's very
    convenient for clients. We need only return the same value for begin()
    and end() and everything works as expected.

    However we need to be a bit more clever for VC 8.  It checks for
    iterator compatibility, i.e. that the iterators are not
    invalidated and that they are for the same container.  It appears
    that default iterators are not compatible with anything.  So we
    use static container for the nil data case.
 */
static Errata::Container NIL_CONTAINER;

Errata::iterator
Errata::begin() {
  return m_data ? m_data->m_items.rbegin() : NIL_CONTAINER.rbegin();
}

Errata::const_iterator
Errata::begin() const {
  return m_data ? static_cast<Data const&>(*m_data).m_items.rbegin()
    : static_cast<Container const&>(NIL_CONTAINER).rbegin();
}

Errata::iterator
Errata::end() {
  return m_data ? m_data->m_items.rend() : NIL_CONTAINER.rend();
}

Errata::const_iterator
Errata::end() const {
  return m_data ? static_cast<Data const&>(*m_data).m_items.rend()
    : static_cast<Container const&>(NIL_CONTAINER).rend();
}

void
Errata::registerSink(Sink::Handle const& s) {
  Sink_List.push_back(s);
}

std::ostream&
Errata::write(
  std::ostream& out,
  int offset,
  int indent,
  int shift,
  char const* lead
) const {

  for ( auto m : *this ) {
    if ((offset + indent) > 0) {
      out << std::setw(indent + offset) << std::setfill(' ')
          << ((indent > 0 && lead) ? lead : " ");
    }

    out << m.m_id << " [" << m.m_code << "]: " << m.m_text
        << std::endl
      ;
    if (m.getErrata().size()) {
      m.getErrata().write(out, offset, indent+shift, shift, lead);
}

  }
  return out;
}

size_t
Errata::write(
  char *buff,
  size_t n,
  int offset,
  int indent,
  int shift,
  char const* lead
) const {
  std::ostringstream out;
  std::string text;
  this->write(out, offset, indent, shift, lead);
  text = out.str();
  memcpy(buff, text.data(), std::min(n, text.size()));
  return text.size();
}

std::ostream& operator<< (std::ostream& os, Errata const& err) {
  return err.write(os, 0, 0, 2, "> ");
}

} // namespace ts
