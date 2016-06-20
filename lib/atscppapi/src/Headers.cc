/**
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

/**
 * @file Headers.cc
 */
#include "atscppapi/Headers.h"
#include "atscppapi/shared_ptr.h"
#include "logging_internal.h"
#include <string>
#include <cstring>
#include <sstream>
#include <ts/ts.h>
#include "atscppapi/noncopyable.h"
#include <cctype>

using atscppapi::Headers;
using atscppapi::HeaderField;
using atscppapi::HeaderFieldName;
using atscppapi::header_field_iterator;
using atscppapi::header_field_value_iterator;

using std::string;

namespace atscppapi
{
HeaderFieldName::HeaderFieldName(const std::string &name)
{
  name_ = name;
}

HeaderFieldName::operator std::string()
{
  return name_;
}

HeaderFieldName::operator const char *()
{
  return name_.c_str();
}

std::string
HeaderFieldName::str()
{
  return name_;
}

HeaderFieldName::size_type
HeaderFieldName::length()
{
  return name_.length();
}

const char *
HeaderFieldName::c_str()
{
  return name_.c_str();
}

bool
HeaderFieldName::operator==(const char *field_name)
{
  return (::strcasecmp(c_str(), field_name) == 0);
}

bool
HeaderFieldName::operator==(const std::string &field_name)
{
  return operator==(field_name.c_str());
}

bool
HeaderFieldName::operator!=(const char *field_name)
{
  return !operator==(field_name);
}

bool
HeaderFieldName::operator!=(const std::string &field_name)
{
  return !operator==(field_name.c_str());
}

/**
 * @private
 */
struct HeaderFieldValueIteratorState : noncopyable {
  TSMBuffer hdr_buf_;
  TSMLoc hdr_loc_;
  TSMLoc field_loc_;
  int index_;
  HeaderFieldValueIteratorState() : hdr_buf_(NULL), hdr_loc_(NULL), field_loc_(NULL), index_(0) {}
  void
  reset(TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc field_loc, int index)
  {
    hdr_buf_   = bufp;
    hdr_loc_   = hdr_loc;
    field_loc_ = field_loc;
    index_     = index;
  }
};

header_field_value_iterator::header_field_value_iterator(void *bufp, void *hdr_loc, void *field_loc, int index)
{
  state_ = new HeaderFieldValueIteratorState();
  state_->reset(static_cast<TSMBuffer>(bufp), static_cast<TSMLoc>(hdr_loc), static_cast<TSMLoc>(field_loc), index);
}

header_field_value_iterator::header_field_value_iterator(const header_field_value_iterator &it)
{
  state_ = new HeaderFieldValueIteratorState();
  state_->reset(it.state_->hdr_buf_, it.state_->hdr_loc_, it.state_->field_loc_, it.state_->index_);
}

header_field_value_iterator::~header_field_value_iterator()
{
  delete state_;
}

std::string header_field_value_iterator::operator*()
{
  if (state_->index_ >= 0) {
    int length      = 0;
    const char *str = TSMimeHdrFieldValueStringGet(state_->hdr_buf_, state_->hdr_loc_, state_->field_loc_, state_->index_, &length);
    if (length && str) {
      return std::string(str, length);
    }
  }
  return std::string();
}

header_field_value_iterator &header_field_value_iterator::operator++()
{
  ++state_->index_;
  return *this;
}

header_field_value_iterator header_field_value_iterator::operator++(int)
{
  header_field_value_iterator tmp(*this);
  operator++();
  return tmp;
}

bool
header_field_value_iterator::operator==(const header_field_value_iterator &rhs) const
{
  return (state_->hdr_buf_ == rhs.state_->hdr_buf_) && (state_->hdr_loc_ == rhs.state_->hdr_loc_) &&
         (state_->field_loc_ == rhs.state_->field_loc_) && (state_->index_ == rhs.state_->index_);
}

bool
header_field_value_iterator::operator!=(const header_field_value_iterator &rhs) const
{
  return !operator==(rhs);
}

/**
 * @private
 */
struct MLocContainer {
  TSMBuffer hdr_buf_;
  TSMLoc hdr_loc_;
  TSMLoc field_loc_;
  MLocContainer(TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc field_loc) : hdr_buf_(bufp), hdr_loc_(hdr_loc), field_loc_(field_loc) {}
  ~MLocContainer()
  {
    if (field_loc_ != TS_NULL_MLOC) {
      TSHandleMLocRelease(hdr_buf_, hdr_loc_, field_loc_);
    }
  }
};

/**
 * @private
 */
struct HeaderFieldIteratorState {
  shared_ptr<MLocContainer> mloc_container_;
  HeaderFieldIteratorState(TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc field_loc)
    : mloc_container_(new MLocContainer(bufp, hdr_loc, field_loc))
  {
  }
};

HeaderField::~HeaderField()
{
}

HeaderField::size_type
HeaderField::size() const
{
  return TSMimeHdrFieldValuesCount(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                   iter_.state_->mloc_container_->field_loc_);
}

header_field_value_iterator
HeaderField::begin()
{
  return header_field_value_iterator(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                     iter_.state_->mloc_container_->field_loc_, 0);
}

header_field_value_iterator
HeaderField::end()
{
  return header_field_value_iterator(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                     iter_.state_->mloc_container_->field_loc_, size());
}

HeaderFieldName
HeaderField::name() const
{
  int length      = 0;
  const char *str = TSMimeHdrFieldNameGet(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                          iter_.state_->mloc_container_->field_loc_, &length);
  if (str && length) {
    return std::string(str, length);
  }
  return std::string();
}

std::string
HeaderField::values(const char *join)
{
  std::string ret;
  for (header_field_value_iterator it = begin(); it != end(); ++it) {
    if (ret.size()) {
      ret.append(join);
    }
    ret.append(*it);
  }
  return ret;
}

std::string
HeaderField::values(const std::string &join)
{
  return values(join.c_str());
}

std::string
HeaderField::values(const char join)
{
  return values(std::string().append(1, join));
}

std::string
Headers::value(const std::string key, size_type index /* = 0 */)
{
  header_field_iterator iter = find(key);
  if (iter == end()) {
    return string();
  }
  if (index == 0) { // skip for loop
    return *((*iter).begin());
  }
  for (; iter != end(); iter.nextDup()) {
    if (index < (*iter).size()) {
      return (*iter)[index];
    }
    index -= (*iter).size();
  }
  return string();
}

bool
HeaderField::empty()
{
  return (begin() == end());
}

bool
HeaderField::clear()
{
  return (TSMimeHdrFieldValuesClear(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                    iter_.state_->mloc_container_->field_loc_) == TS_SUCCESS);
}

bool
HeaderField::erase(header_field_value_iterator it)
{
  return (TSMimeHdrFieldValueDelete(it.state_->hdr_buf_, it.state_->hdr_loc_, it.state_->field_loc_, it.state_->index_) ==
          TS_SUCCESS);
}

bool
HeaderField::append(const std::string &value)
{
  return append(value.c_str(), value.length());
}

bool
HeaderField::append(const char *value, int length)
{
  return (TSMimeHdrFieldValueStringInsert(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                          iter_.state_->mloc_container_->field_loc_, -1, value, -1) == TS_SUCCESS);
}

bool
HeaderField::setName(const std::string &str)
{
  return (TSMimeHdrFieldNameSet(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                iter_.state_->mloc_container_->field_loc_, str.c_str(), str.length()) == TS_SUCCESS);
}

bool
HeaderField::operator==(const char *field_name) const
{
  return (::strcasecmp(name(), field_name) == 0);
}

bool
HeaderField::operator==(const std::string &field_name) const
{
  return operator==(field_name.c_str());
}

bool
HeaderField::operator!=(const char *field_name) const
{
  return !operator==(field_name);
}

bool
HeaderField::operator!=(const std::string &field_name) const
{
  return !operator==(field_name.c_str());
}

bool
HeaderField::operator=(const std::string &field_value)
{
  if (!clear())
    return false;

  return append(field_value);
}

bool
HeaderField::operator=(const char *field_value)
{
  if (!clear())
    return false;

  return append(field_value);
}

std::string HeaderField::operator[](const int index)
{
  return *header_field_value_iterator(iter_.state_->mloc_container_->hdr_buf_, iter_.state_->mloc_container_->hdr_loc_,
                                      iter_.state_->mloc_container_->field_loc_, index);
}

std::string
HeaderField::str()
{
  std::ostringstream oss;
  oss << (*this);
  return oss.str();
}

std::ostream &
operator<<(std::ostream &os, HeaderField &obj)
{
  os << obj.name() << ": ";
  int count = obj.size();
  for (HeaderField::iterator it = obj.begin(); it != obj.end(); ++it) {
    os << (*it);
    if (--count > 0)
      os << ",";
  }
  return os;
}

header_field_iterator::header_field_iterator(void *hdr_buf, void *hdr_loc, void *field_loc)
  : state_(
      new HeaderFieldIteratorState(static_cast<TSMBuffer>(hdr_buf), static_cast<TSMLoc>(hdr_loc), static_cast<TSMLoc>(field_loc)))
{
}

header_field_iterator::header_field_iterator(const header_field_iterator &it) : state_(new HeaderFieldIteratorState(*it.state_))
{
}

header_field_iterator &
header_field_iterator::operator=(const header_field_iterator &rhs)
{
  if (this != &rhs) {
    delete state_;
    state_ = new HeaderFieldIteratorState(*rhs.state_);
  }
  return *this;
}

header_field_iterator::~header_field_iterator()
{
  delete state_;
}

// utility function to use to advance iterators using different functions
HeaderFieldIteratorState *
advanceIterator(HeaderFieldIteratorState *state, TSMLoc (*getNextField)(TSMBuffer, TSMLoc, TSMLoc))
{
  if (state->mloc_container_->field_loc_ != TS_NULL_MLOC) {
    TSMBuffer hdr_buf     = state->mloc_container_->hdr_buf_;
    TSMLoc hdr_loc        = state->mloc_container_->hdr_loc_;
    TSMLoc next_field_loc = getNextField(hdr_buf, hdr_loc, state->mloc_container_->field_loc_);
    delete state;
    state = new HeaderFieldIteratorState(hdr_buf, hdr_loc, next_field_loc);
  }
  return state;
}

header_field_iterator &header_field_iterator::operator++()
{
  state_ = advanceIterator(state_, TSMimeHdrFieldNext);
  return *this;
}

header_field_iterator header_field_iterator::operator++(int)
{
  header_field_iterator tmp(*this);
  operator++();
  return tmp;
}

header_field_iterator &
header_field_iterator::nextDup()
{
  state_ = advanceIterator(state_, TSMimeHdrFieldNextDup);
  return *this;
}

bool
header_field_iterator::operator==(const header_field_iterator &rhs) const
{
  return (state_->mloc_container_->hdr_buf_ == rhs.state_->mloc_container_->hdr_buf_) &&
         (state_->mloc_container_->hdr_loc_ == rhs.state_->mloc_container_->hdr_loc_) &&
         (state_->mloc_container_->field_loc_ == rhs.state_->mloc_container_->field_loc_);
}

bool
header_field_iterator::operator!=(const header_field_iterator &rhs) const
{
  return !operator==(rhs);
}

HeaderField header_field_iterator::operator*()
{
  return HeaderField(*this);
}

/**
 * @private
 */
struct HeadersState : noncopyable {
  TSMBuffer hdr_buf_;
  TSMLoc hdr_loc_;
  bool self_created_structures_;
  HeadersState()
  {
    hdr_buf_                 = TSMBufferCreate();
    hdr_loc_                 = TSHttpHdrCreate(hdr_buf_);
    self_created_structures_ = true;
  }
  void
  reset(TSMBuffer bufp, TSMLoc hdr_loc)
  {
    if (self_created_structures_) {
      TSHandleMLocRelease(hdr_buf_, TS_NULL_MLOC /* no parent */, hdr_loc_);
      TSMBufferDestroy(hdr_buf_);
      self_created_structures_ = false;
    }
    hdr_buf_ = bufp;
    hdr_loc_ = hdr_loc;
  }
  ~HeadersState() { reset(NULL, NULL); }
};

Headers::Headers()
{
  state_ = new HeadersState();
}

Headers::Headers(void *bufp, void *mloc)
{
  state_ = new HeadersState();
  reset(bufp, mloc);
}

void
Headers::reset(void *bufp, void *mloc)
{
  state_->reset(static_cast<TSMBuffer>(bufp), static_cast<TSMLoc>(mloc));
}

Headers::~Headers()
{
  delete state_;
}

bool
Headers::isInitialized() const
{
  return (state_->hdr_buf_ && state_->hdr_loc_);
}

bool
Headers::empty()
{
  return (begin() == end());
}

Headers::size_type
Headers::size() const
{
  return TSMimeHdrFieldsCount(state_->hdr_buf_, state_->hdr_loc_);
}

Headers::size_type
Headers::lengthBytes() const
{
  return TSMimeHdrLengthGet(state_->hdr_buf_, state_->hdr_loc_);
}

header_field_iterator
Headers::begin()
{
  return header_field_iterator(state_->hdr_buf_, state_->hdr_loc_, TSMimeHdrFieldGet(state_->hdr_buf_, state_->hdr_loc_, 0));
}

header_field_iterator
Headers::end()
{
  return header_field_iterator(state_->hdr_buf_, state_->hdr_loc_, TS_NULL_MLOC);
}

bool
Headers::clear()
{
  return (TSMimeHdrFieldsClear(state_->hdr_buf_, state_->hdr_loc_) == TS_SUCCESS);
}

bool
Headers::erase(header_field_iterator it)
{
  return (TSMimeHdrFieldDestroy(it.state_->mloc_container_->hdr_buf_, it.state_->mloc_container_->hdr_loc_,
                                it.state_->mloc_container_->field_loc_) == TS_SUCCESS);
}

Headers::size_type
Headers::erase(const std::string &key)
{
  return erase(key.c_str(), key.length());
}

Headers::size_type
Headers::erase(const char *key, int length)
{
  header_field_iterator iter = find(key, length);
  size_type erased_count     = 0;
  while (iter != end()) {
    header_field_iterator iter_to_delete = iter;
    iter.nextDup();
    erase(iter_to_delete);
    ++erased_count;
  }
  return erased_count;
}

Headers::size_type
Headers::count(const char *key, int length)
{
  size_type ret_count = 0;
  for (header_field_iterator it = begin(); it != end(); ++it) {
    if ((*it).name() == key) {
      ret_count++;
    }
  }
  return ret_count;
}

Headers::size_type
Headers::count(const std::string &key)
{
  return count(key.c_str(), key.length());
}

std::string
Headers::values(const std::string &key, const char *join)
{
  std::string ret;
  for (header_field_iterator it = find(key); it != end(); it.nextDup()) {
    if (ret.size()) {
      ret.append(join);
    }
    ret.append((*it).values(join));
  }

  return ret;
}

std::string
Headers::values(const std::string &key, const std::string &join)
{
  return values(key, join.c_str());
}

std::string
Headers::values(const std::string &key, const char join)
{
  return values(key, std::string().assign(1, join));
}

header_field_iterator
Headers::find(const std::string &key)
{
  return find(key.c_str(), key.length());
}

header_field_iterator
Headers::find(const char *key, int length)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(state_->hdr_buf_, state_->hdr_loc_, key, length);
  if (field_loc != TS_NULL_MLOC)
    return header_field_iterator(state_->hdr_buf_, state_->hdr_loc_, field_loc);

  return end();
}

Headers::iterator
Headers::append(const std::string &key, const std::string &value)
{
  TSMLoc field_loc = TS_NULL_MLOC;

  if (TSMimeHdrFieldCreate(state_->hdr_buf_, state_->hdr_loc_, &field_loc) == TS_SUCCESS) {
    TSMimeHdrFieldNameSet(state_->hdr_buf_, state_->hdr_loc_, field_loc, key.c_str(), key.length());
    TSMimeHdrFieldAppend(state_->hdr_buf_, state_->hdr_loc_, field_loc);
    TSMimeHdrFieldValueStringInsert(state_->hdr_buf_, state_->hdr_loc_, field_loc, 0, value.c_str(), value.length());
    return header_field_iterator(state_->hdr_buf_, state_->hdr_loc_, field_loc);
  } else
    return end();
}

Headers::iterator
Headers::set(const std::string &key, const std::string &value)
{
  erase(key);
  return append(key, value);
}

HeaderField Headers::operator[](const std::string &key)
{
  // In STL fashion if the key doesn't exist it will be added with an empty value.
  header_field_iterator it = find(key);
  if (it != end()) {
    return *it;
  } else {
    return *append(key, "");
  }
}

std::string
Headers::str()
{
  std::ostringstream oss;
  oss << (*this);
  return oss.str();
}

std::string
Headers::wireStr()
{
  string retval;
  for (iterator iter = begin(), last = end(); iter != last; ++iter) {
    HeaderField hf = *iter;
    retval += hf.name().str();
    retval += ": ";
    retval += hf.values(", ");
    retval += "\r\n";
  }
  return retval;
}

std::ostream &
operator<<(std::ostream &os, atscppapi::Headers &obj)
{
  for (header_field_iterator it = obj.begin(); it != obj.end(); ++it) {
    HeaderField hf = *it;
    os << hf << std::endl;
  }
  return os;
}

} /* atscppapi namespace */
