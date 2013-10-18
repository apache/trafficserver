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
#include "InitializableValue.h"
#include "logging_internal.h"
#include <ts/ts.h>
#include "atscppapi/noncopyable.h"
#include <cctype>

using atscppapi::Headers;
using std::string;
using std::list;
using std::pair;
using std::make_pair;
using std::ostringstream;
using std::map;

namespace atscppapi {

namespace {
  const int APPEND_INDEX = -1;
  const list<string> EMPTY_VALUE_LIST;
  const int FIRST_INDEX = 0;
}

/**
 * @private
 */
struct HeadersState: noncopyable {
  Headers::Type type_;
  TSMBuffer hdr_buf_;
  TSMLoc hdr_loc_;
  InitializableValue<Headers::NameValuesMap> name_values_map_;
  bool detached_;
  InitializableValue<Headers::RequestCookieMap> request_cookies_;
  InitializableValue<list<Headers::ResponseCookie> > response_cookies_;
  HeadersState(Headers::Type type) : type_(type), hdr_buf_(NULL), hdr_loc_(NULL), detached_(false) { }
};

}

Headers::Headers(Type type) {
  state_ = new HeadersState(type);
}

Headers::Type Headers::getType() const {
  return state_->type_;
}

void Headers::setType(Type type) {
  state_->type_ = type;
}

void Headers::init(void *hdr_buf, void *hdr_loc) {
  if (state_->hdr_buf_ || state_->hdr_loc_ || state_->detached_) {
    LOG_ERROR("Cannot reinitialize; hdr_buf %p, hdr_loc %p, detached %s", state_->hdr_buf_, state_->hdr_loc_,
              (state_->detached_ ? "true" : "false"));
    return;
  }
  state_->hdr_buf_ = static_cast<TSMBuffer>(hdr_buf);
  state_->hdr_loc_ = static_cast<TSMLoc>(hdr_loc);
}

void Headers::initDetached() {
  if (state_->hdr_buf_ || state_->hdr_loc_ || state_->detached_) {
    LOG_ERROR("Cannot reinitialize; hdr_buf %p, hdr_loc %p, detached %s", state_->hdr_buf_, state_->hdr_loc_,
              (state_->detached_ ? "true" : "false"));
    return;
  }
  state_->detached_ = true;
  state_->name_values_map_.setInitialized();
}

namespace {

void extractHeaderFieldValues(TSMBuffer hdr_buf, TSMLoc hdr_loc, TSMLoc field_loc, const string &header_name,
                              list<string> &value_list) {
  int num_values = TSMimeHdrFieldValuesCount(hdr_buf, hdr_loc, field_loc);
  LOG_DEBUG("Got %d values for header [%s]", num_values, header_name.c_str());
  if (!num_values) {
    LOG_DEBUG("Header [%s] has no values; Adding empty string", header_name.c_str());
    value_list.push_back(string());
    return;
  }
  const char *value;
  int value_len;
  for (int i = 0; i < num_values; ++i) {
    value = TSMimeHdrFieldValueStringGet(hdr_buf, hdr_loc, field_loc, i, &value_len);
    value_list.push_back((value && value_len) ? string(value, value_len) : string());
    LOG_DEBUG("Added value [%.*s] to header [%s]", value_len, value, header_name.c_str());
  }
}

}

bool Headers::checkAndInitHeaders() const {
  if (state_->name_values_map_.isInitialized()) {
    return true;
  } else if ((state_->hdr_buf_ == NULL) || (state_->hdr_loc_ == NULL)) {
    LOG_ERROR("Failed to initialize! TS header handles not set; hdr_buf %p, hdr_loc %p", state_->hdr_buf_, 
              state_->hdr_loc_);
    return false;
  }
  state_->name_values_map_.getValueRef().clear();
  string key;
  const char *name, *value;
  int name_len, num_values, value_len;
  pair<NameValuesMap::iterator, bool> insert_result;
  TSMLoc field_loc = TSMimeHdrFieldGet(state_->hdr_buf_, state_->hdr_loc_, FIRST_INDEX);
  while (field_loc) {
    name = TSMimeHdrFieldNameGet(state_->hdr_buf_, state_->hdr_loc_, field_loc, &name_len);
    if (name && (name_len > 0)) {
      key.assign(name, name_len);
      insert_result = state_->name_values_map_.getValueRef().insert(
        NameValuesMap::value_type(key, EMPTY_VALUE_LIST));
      NameValuesMap::iterator &inserted_element = insert_result.first;
      list<string> &value_list = inserted_element->second;
      extractHeaderFieldValues(state_->hdr_buf_, state_->hdr_loc_, field_loc, key, value_list);
    } else {
      LOG_ERROR("Failed to get name of header; hdr_buf %p, hdr_loc %p", state_->hdr_buf_, state_->hdr_loc_);
    }
    TSMLoc next_field_loc = TSMimeHdrFieldNext(state_->hdr_buf_, state_->hdr_loc_, field_loc);
    TSHandleMLocRelease(state_->hdr_buf_, state_->hdr_loc_, field_loc);
    field_loc = next_field_loc;
  }
  state_->name_values_map_.setInitialized();
  LOG_DEBUG("Initialized headers map");
  return true;
}

Headers::~Headers() {
  delete state_;
}

Headers::size_type Headers::erase(const string &k) {
  if (!checkAndInitHeaders()) {
    return 0;
  }
  if ((state_->type_ == TYPE_REQUEST) && (CaseInsensitiveStringComparator()(k, "Cookie") == 0)) {
    state_->request_cookies_.getValueRef().clear();
    state_->request_cookies_.setInitialized(false);
  } else if ((state_->type_ == TYPE_RESPONSE) && (CaseInsensitiveStringComparator()(k, "Set-Cookie") == 0)) {
    state_->response_cookies_.getValueRef().clear();
    state_->response_cookies_.setInitialized(false);
  }
  LOG_DEBUG("Erasing header [%s]", k.c_str());
  return doBasicErase(k);
}

Headers::size_type Headers::doBasicErase(const string &k) {
  if (!state_->detached_) {
    TSMLoc field_loc = TSMimeHdrFieldFind(state_->hdr_buf_, state_->hdr_loc_, k.c_str(), k.length());
    while (field_loc) {
      TSMLoc next_field_loc = TSMimeHdrFieldNextDup(state_->hdr_buf_, state_->hdr_loc_, field_loc);
      TSMimeHdrFieldDestroy(state_->hdr_buf_, state_->hdr_loc_, field_loc);
      TSHandleMLocRelease(state_->hdr_buf_, state_->hdr_loc_, field_loc);
      field_loc = next_field_loc;
    }
  }
  return state_->name_values_map_.getValueRef().erase(k);
}

Headers::const_iterator Headers::set(const pair<string, list<string> > &pair) {
  erase(pair.first);
  return append(pair);
}


Headers::const_iterator Headers::set(const string &key, const list<string> &val) {
  return set(make_pair(key,val));
}

Headers::const_iterator Headers::set(const string &key, const string &val) {
  list<string> values;
  values.push_back(val);
  return set(make_pair(key,values));
}

Headers::const_iterator Headers::append(const pair<string, list<string> > &pair) {
  if (!checkAndInitHeaders()) {
    return state_->name_values_map_.getValueRef().end();
  }
  if ((state_->type_ == TYPE_REQUEST) && (CaseInsensitiveStringComparator()(pair.first, "Cookie") == 0)) {
    state_->request_cookies_.getValueRef().clear();
    state_->request_cookies_.setInitialized(false);
  } else if ((state_->type_ == TYPE_RESPONSE) &&
             (CaseInsensitiveStringComparator()(pair.first, "Set-Cookie") == 0)) {
    state_->response_cookies_.getValueRef().clear();
    state_->response_cookies_.setInitialized(false);
  }
  return doBasicAppend(pair);
}

Headers::const_iterator Headers::doBasicAppend(const pair<string, list<string> > &pair) {
  const string &header_name = pair.first; // handy references
  const list<string> &new_values = pair.second;

  std::pair<NameValuesMap::iterator, bool> insert_result;
  if (state_->detached_) {
    insert_result = state_->name_values_map_.getValueRef().insert(make_pair(header_name, EMPTY_VALUE_LIST));
    list<string> &value_list = insert_result.first->second; // existing or newly inserted
    for (list<string>::const_iterator iter = new_values.begin(), end = new_values.end(); iter != end; ++iter) {
      value_list.push_back(*iter);
      LOG_DEBUG("Appended value [%s] to header [%s]", iter->c_str(), header_name.c_str());
    }
  } else {
    TSMLoc field_loc =
      TSMimeHdrFieldFind(state_->hdr_buf_, state_->hdr_loc_, header_name.c_str(), header_name.length());
    
    if (!field_loc) {
      TSMimeHdrFieldCreate(state_->hdr_buf_, state_->hdr_loc_, &field_loc);
      TSMimeHdrFieldNameSet(state_->hdr_buf_, state_->hdr_loc_, field_loc, header_name.c_str(), header_name.length());
      TSMimeHdrFieldAppend(state_->hdr_buf_, state_->hdr_loc_, field_loc);
      LOG_DEBUG("Created new structure for header [%s]", header_name.c_str());
    }
    for(list<string>::const_iterator ii = new_values.begin(); ii != new_values.end(); ++ii) {
      TSMimeHdrFieldValueStringInsert(state_->hdr_buf_, state_->hdr_loc_, field_loc, APPEND_INDEX, (*ii).c_str(),
                                      (*ii).length());
    }

    insert_result = state_->name_values_map_.getValueRef().insert(make_pair(header_name, EMPTY_VALUE_LIST));
    list<string> &value_list = insert_result.first->second; // existing or newly inserted

    //
    // Now because TSMimeHdrFieldValueStringInsert will (possibly) parse each value for commas, that is,
    // if you insert a list of three elements "Foo", "Bar,Baz", "Blah", this would become
    // four elements in the marshal buffer and we need to update our own map to reflect that.
    //
    // Rather than inserting the list<strings> directly into our map, we will actually rebuild it using the
    // Traffic Server HDR Marshal Buffer so we're 100% consistent with the internal representation.
    //
    if (!insert_result.second) {
      value_list.clear();
    }
    extractHeaderFieldValues(state_->hdr_buf_, state_->hdr_loc_, field_loc, header_name, value_list);
    TSHandleMLocRelease(state_->hdr_buf_, state_->hdr_loc_, field_loc);
    LOG_DEBUG("Header [%s] has value(s) [%s]", header_name.c_str(), getJoinedValues(value_list).c_str());
  }
  return insert_result.first;
}

string Headers::getJoinedValues(const string &key, char value_delimiter) {
  checkAndInitHeaders();

  string ret;
  Headers::NameValuesMap::iterator key_iter = state_->name_values_map_.getValueRef().find(key);
  if (key_iter == state_->name_values_map_.getValueRef().end()) {
    LOG_DEBUG("Header [%s] not present", key.c_str());
    return ret;
  }
  return getJoinedValues(key_iter->second);
}

string Headers::getJoinedValues(const list<string> &values, char delimiter) {
  string ret;
  ret.reserve(128);
  for (list<string>::const_iterator vals_iter = values.begin(), vals_end = values.end(); vals_iter != vals_end;
       ++vals_iter) {
    if (!ret.empty()) {
      ret += delimiter;
    }
    ret.append(*vals_iter);
  }
  return ret;
}

Headers::const_iterator Headers::append(const string &key, const list<string> &val) {
  return append(make_pair(key,val));
}

Headers::const_iterator Headers::append(const string &key, const string &val) {
  list<string> values;
  values.push_back(val);
  return append(make_pair(key,values));
}

Headers::const_iterator Headers::begin() const {
  checkAndInitHeaders();
  return state_->name_values_map_.getValueRef().begin();
}

Headers::const_iterator Headers::end() const {
  return state_->name_values_map_.getValueRef().end();
}

Headers::const_reverse_iterator Headers::rbegin() const {
  checkAndInitHeaders();
  return state_->name_values_map_.getValueRef().rbegin();
}

Headers::const_reverse_iterator Headers::rend() const {
  return state_->name_values_map_.getValueRef().rend();
}

Headers::const_iterator Headers::find(const string &k) const {
  checkAndInitHeaders();
  return state_->name_values_map_.getValueRef().find(k);
}

Headers::size_type Headers::count(const string &key) const {
  checkAndInitHeaders();
  return state_->name_values_map_.getValueRef().count(key);
}

bool Headers::empty() const {
  checkAndInitHeaders();
  return state_->name_values_map_.getValueRef().empty();
}

Headers::size_type Headers::max_size() const {
  return state_->name_values_map_.getValueRef().max_size();
}

Headers::size_type Headers::size() const {
  checkAndInitHeaders();
  return state_->name_values_map_.getValueRef().size();
}

namespace {

const list<string> EMPTY_REQUEST_COOKIE_VALUE_LIST;

void stripEnclosingWhitespace(string &token) {
  size_t token_size = token.size(), i;
  for (i = 0; (i < token_size) && std::isspace(token[i]); ++i);
  if (i) {
    token.erase(0, i);
    token_size -= i;
  }
  // can't use >= 0 here as we size_t will never go negative
  for (i = token_size; (i > 0) && std::isspace(token[i - 1]); --i);
  if (token_size - i) {
    token.resize(i);
  }
}

void addCookieToMap(Headers::RequestCookieMap &cookie_map, const string &name, const string &value) {
  Headers::RequestCookieMap::value_type element_to_insert(name, EMPTY_REQUEST_COOKIE_VALUE_LIST);
  list<string> &value_list = (cookie_map.insert(element_to_insert)).first->second;
  if (!value_list.empty()) { // log duplicates
    LOG_DEBUG("Cookie [%s] has multiple instances", name.c_str());
  }
  value_list.push_back(value);
  LOG_DEBUG("Added cookie [%s] with value [%s]", name.c_str(), value.c_str());
}

}

const Headers::RequestCookieMap &Headers::getRequestCookies() const {
  if (state_->type_ != Headers::TYPE_REQUEST) {
    LOG_ERROR("Object is not of type request. Returning empty map");
    return state_->request_cookies_;
  }
  if (state_->request_cookies_.isInitialized() || !checkAndInitHeaders()) {
    return state_->request_cookies_;
  }
  state_->request_cookies_.setInitialized();
  const_iterator iter = find("Cookie");
  if (iter != end()) {
    const list<string> &cookie_kvs = iter->second; // cookie key-value pairs
    string name, value;
    for (list<string>::const_iterator cookie_kv_iter = cookie_kvs.begin(), cookie_kv_end = cookie_kvs.end();
         cookie_kv_iter != cookie_kv_end; ++cookie_kv_iter) {
      const string &cookie_kv = *cookie_kv_iter;
      size_t cookie_kv_size = cookie_kv.size();
      size_t start_pos, end_pos, length;
        
      for (size_t i = 0; i < cookie_kv_size; ) {
        start_pos = i;
        for (end_pos = start_pos;
             (end_pos < cookie_kv_size) && (cookie_kv[end_pos] != '=') && (cookie_kv[end_pos] != ';'); ++end_pos);
        if ((end_pos == cookie_kv_size) || (cookie_kv[end_pos] == ';')) {
          LOG_DEBUG("Unexpected end in cookie key value string [%s]", cookie_kv.c_str());
          return state_->request_cookies_;
        }
        length = end_pos - start_pos;
        name.assign(cookie_kv, start_pos, length);
        stripEnclosingWhitespace(name);
        if (name.empty()) {
          LOG_DEBUG("Empty cookie name in key value string [%s]", cookie_kv.c_str());
          return state_->request_cookies_;
        }
        start_pos = ++end_pos; // value should start here
        if (start_pos == cookie_kv_size)  {
          LOG_DEBUG("Cookie [%s] has no value in key value string [%s]", name.c_str(), cookie_kv.c_str());
          return state_->request_cookies_;
        }
        bool within_quotes = false;
        for (end_pos = start_pos; end_pos < cookie_kv_size; ++end_pos) {
          if (cookie_kv[end_pos] == '"') {
            within_quotes = !within_quotes;
          } else if ((cookie_kv[end_pos] == ';') && !within_quotes) {
            break;
          }
        }
        length = end_pos - start_pos;
        value.assign(cookie_kv, start_pos, length);
        stripEnclosingWhitespace(value);
        addCookieToMap(state_->request_cookies_, name ,value);
        i = ++end_pos; // next name should start here
      }
    }
  }
  return state_->request_cookies_;
}

const list<Headers::ResponseCookie> &Headers::getResponseCookies() const {
  if (state_->type_ != Headers::TYPE_RESPONSE) {
    LOG_ERROR("Object is not of type response. Returning empty list");
    return state_->response_cookies_;
  }
  if (state_->response_cookies_.isInitialized() || !checkAndInitHeaders()) {
    return state_->response_cookies_;
  }
  state_->response_cookies_.setInitialized();
  // @TODO Parse Set-Cookie headers here
  return state_->response_cookies_;
}

bool Headers::addCookie(const string &name, const string &value) {
  if (state_->type_ != Headers::TYPE_REQUEST) {
    LOG_ERROR("Cannot add request cookie to response headers");
    return false;
  }
  if (!checkAndInitHeaders()) {
    return false;
  }
  addCookieToMap(state_->request_cookies_, name, value);
  updateRequestCookieHeaderFromMap();
  return true;
}

bool Headers::addCookie(const ResponseCookie &response_cookie) {
  if (state_->type_ != Headers::TYPE_RESPONSE) {
    LOG_ERROR("Cannot add response cookie to object not of type response");
    return false;
  }
  if (!checkAndInitHeaders()) {
    false;
  }
  // @TODO Do logic here
  return true;
}
  
bool Headers::setCookie(const string &name, const string &value) {
  if (state_->type_ != Headers::TYPE_REQUEST) {
    LOG_ERROR("Cannot set request cookie to response headers");
    return false;
  }
  if (!checkAndInitHeaders()) {
    return false;
  }
  getRequestCookies();
  state_->request_cookies_.getValueRef().erase(name);
  return addCookie(name, value);
}

bool Headers::setCookie(const ResponseCookie &response_cookie) {
  if (state_->type_ != Headers::TYPE_RESPONSE) {
    LOG_ERROR("Cannot set response cookie to request headers");
    return false;
  }
  if (!checkAndInitHeaders()) {
    return false;
  }
  // @TODO Do logic here
  return true;
}

bool Headers::deleteCookie(const string &name) {
  if (!checkAndInitHeaders()) {
    return false;
  }
  if (state_->type_ == TYPE_REQUEST) {
    getRequestCookies();
    RequestCookieMap::iterator iter = state_->request_cookies_.getValueRef().find(name);
    if (iter == state_->request_cookies_.getValueRef().end()) {
      LOG_DEBUG("Cookie [%s] doesn't exist", name.c_str());
      return true;
    }
    state_->request_cookies_.getValueRef().erase(iter);
    updateRequestCookieHeaderFromMap();
    return true;
  }
  getResponseCookies();
  // @TODO Do logic here
  return true;
}

void Headers::updateRequestCookieHeaderFromMap() {
  string cookie_header;
  for (RequestCookieMap::iterator cookie_iter = state_->request_cookies_.getValueRef().begin(), 
         cookie_end = state_->request_cookies_.getValueRef().end(); cookie_iter != cookie_end; ++cookie_iter) {
    for (list<string>::iterator value_iter = cookie_iter->second.begin(), value_end = cookie_iter->second.end();
         value_iter != value_end; ++value_iter) {
      cookie_header += cookie_iter->first;
      cookie_header += '=';
      cookie_header += *value_iter;
      cookie_header += "; ";
    }
  }
  if (!cookie_header.empty()) {
    cookie_header.erase(cookie_header.size() - 2, 2); // erase trailing '; '
  }

  // we could have called set(), but set() invalidates the cookie map
  // indirectly by calling append(). But our map is up to date. So we
  // do put the set() logic here explicitly.
  doBasicErase("Cookie");
  list<string> values;
  values.push_back(cookie_header);
  doBasicAppend(pair<string, list<string> >("Cookie", values));
}
