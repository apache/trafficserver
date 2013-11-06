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
 * @file Response.cc
 */
#include "atscppapi/Response.h"
#include "InitializableValue.h"
#include "atscppapi/noncopyable.h"
#include "utils_internal.h"
#include "logging_internal.h"

using namespace atscppapi;
using std::string;

namespace atscppapi {

/**
 * @private
 */
struct ResponseState: noncopyable {
  TSMBuffer hdr_buf_;
  TSMLoc hdr_loc_;
  InitializableValue<HttpVersion> version_;
  InitializableValue<HttpStatus> status_code_;
  InitializableValue<string> reason_phrase_;
  Headers headers_;
  ResponseState() : hdr_buf_(NULL), hdr_loc_(NULL), version_(HTTP_VERSION_UNKNOWN, false), status_code_(HTTP_STATUS_UNKNOWN, false) { }
};

}

Response::Response() {
  state_ = new ResponseState();
//  state_->headers_.setType(Headers::TYPE_RESPONSE);
}

void Response::init(void *hdr_buf, void *hdr_loc) {
  state_->hdr_buf_ = static_cast<TSMBuffer>(hdr_buf);
  state_->hdr_loc_ = static_cast<TSMLoc>(hdr_loc);
  state_->headers_.reset(state_->hdr_buf_, state_->hdr_loc_);
  LOG_DEBUG("Initializing response %p with hdr_buf=%p and hdr_loc=%p", this, state_->hdr_buf_, state_->hdr_loc_);
}

HttpVersion Response::getVersion() const {
  if (state_->version_.isInitialized()) {
    return state_->version_;
  }
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    state_->version_ = utils::internal::getHttpVersion(state_->hdr_buf_, state_->hdr_loc_);
    LOG_DEBUG("Initializing response version to %d [%s] with hdr_buf=%p and hdr_loc=%p",
        state_->version_.getValue(), HTTP_VERSION_STRINGS[state_->version_.getValue()].c_str(), state_->hdr_buf_, state_->hdr_loc_);
    return state_->version_;
  }
  return HTTP_VERSION_UNKNOWN;
}

HttpStatus Response::getStatusCode() const {
  if (state_->status_code_.isInitialized()) {
    return state_->status_code_;
  }
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    state_->status_code_ = static_cast<HttpStatus>(TSHttpHdrStatusGet(state_->hdr_buf_, state_->hdr_loc_));
    LOG_DEBUG("Initializing response status code to %d with hdr_buf=%p and hdr_loc=%p",
        state_->status_code_.getValue(), state_->hdr_buf_, state_->hdr_loc_);
    return state_->status_code_;
  }

  return HTTP_STATUS_UNKNOWN;
}

void Response::setStatusCode(HttpStatus code) {
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    TSHttpHdrStatusSet(state_->hdr_buf_, state_->hdr_loc_, static_cast<TSHttpStatus>(code));
    state_->status_code_ = code;
    LOG_DEBUG("Changing response status code to %d with hdr_buf=%p and hdr_loc=%p",
        state_->status_code_.getValue(), state_->hdr_buf_, state_->hdr_loc_);
  }
}

const string &Response::getReasonPhrase() const {
  if (!state_->reason_phrase_.isInitialized() && state_->hdr_buf_ && state_->hdr_loc_) {
    int length;
    const char *str = TSHttpHdrReasonGet(state_->hdr_buf_, state_->hdr_loc_, &length);
    if (str && length) {
      state_->reason_phrase_.getValueRef().assign(str, length);
      LOG_DEBUG("Initializing response reason phrase to '%s' with hdr_buf=%p and hdr_loc=%p",
          state_->reason_phrase_.getValueRef().c_str(), state_->hdr_buf_, state_->hdr_loc_);
    } else {
      LOG_ERROR("TSHttpHdrReasonGet returned null string or zero length. str=%p, length=%d, hdr_buf=%p, hdr_loc=%p",
          str, length, state_->hdr_buf_, state_->hdr_loc_);
    }
  }
  return state_->reason_phrase_; // if not initialized, we will just return an empty string
}

void Response::setReasonPhrase(const string &phrase) {
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    TSHttpHdrReasonSet(state_->hdr_buf_, state_->hdr_loc_, phrase.c_str(), phrase.length());
    state_->reason_phrase_ = phrase;
    LOG_DEBUG("Changing response reason phrase to '%s' with hdr_buf=%p and hdr_loc=%p",
        phrase.c_str(), state_->hdr_buf_, state_->hdr_loc_);
  }
}

Headers &Response::getHeaders() const {
  return state_->headers_;  // if not initialized, we will just return an empty object
}

Response::~Response() {
  delete state_;
}
