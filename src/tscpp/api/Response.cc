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
#include "tscpp/api/Response.h"
#include "tscpp/api/noncopyable.h"
#include "utils_internal.h"
#include "logging_internal.h"

using namespace atscppapi;
using std::string;

namespace atscppapi
{
/**
 * @private
 */
struct ResponseState : noncopyable {
  TSMBuffer hdr_buf_ = nullptr;
  TSMLoc hdr_loc_    = nullptr;
  Headers headers_;
  ResponseState() = default;
};
} // namespace atscppapi

Response::Response()
{
  state_ = new ResponseState();
  //  state_->headers_.setType(Headers::TYPE_RESPONSE);
}

void
Response::init(void *hdr_buf, void *hdr_loc)
{
  reset();
  if (!hdr_buf || !hdr_loc) {
    return;
  }
  state_->hdr_buf_ = static_cast<TSMBuffer>(hdr_buf);
  state_->hdr_loc_ = static_cast<TSMLoc>(hdr_loc);
  state_->headers_.reset(state_->hdr_buf_, state_->hdr_loc_);
  LOG_DEBUG("Initializing response %p with hdr_buf=%p and hdr_loc=%p", this, state_->hdr_buf_, state_->hdr_loc_);
}

void
Response::reset()
{
  state_->hdr_buf_ = nullptr;
  state_->hdr_loc_ = nullptr;
  state_->headers_.reset(nullptr, nullptr);
  LOG_DEBUG("Reset response %p", this);
}

HttpVersion
Response::getVersion() const
{
  HttpVersion ret_val = HTTP_VERSION_UNKNOWN;
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    ret_val = utils::internal::getHttpVersion(state_->hdr_buf_, state_->hdr_loc_);
    LOG_DEBUG("Initializing response version to %d [%s] with hdr_buf=%p and hdr_loc=%p", ret_val,
              HTTP_VERSION_STRINGS[ret_val].c_str(), state_->hdr_buf_, state_->hdr_loc_);
  }
  return ret_val;
}

HttpStatus
Response::getStatusCode() const
{
  HttpStatus ret_val = HTTP_STATUS_UNKNOWN;
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    ret_val = static_cast<HttpStatus>(TSHttpHdrStatusGet(state_->hdr_buf_, state_->hdr_loc_));
    LOG_DEBUG("Initializing response status code to %d with hdr_buf=%p and hdr_loc=%p", ret_val, state_->hdr_buf_,
              state_->hdr_loc_);
  }
  return ret_val;
}

void
Response::setStatusCode(HttpStatus code)
{
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    TSHttpHdrStatusSet(state_->hdr_buf_, state_->hdr_loc_, static_cast<TSHttpStatus>(code));
    LOG_DEBUG("Changing response status code to %d with hdr_buf=%p and hdr_loc=%p", code, state_->hdr_buf_, state_->hdr_loc_);
  }
}

string
Response::getReasonPhrase() const
{
  string ret_str;
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    int length;
    const char *str = TSHttpHdrReasonGet(state_->hdr_buf_, state_->hdr_loc_, &length);
    if (str && length) {
      ret_str.assign(str, length);
      LOG_DEBUG("Initializing response reason phrase to '%s' with hdr_buf=%p and hdr_loc=%p", ret_str.c_str(), state_->hdr_buf_,
                state_->hdr_loc_);
    } else {
      LOG_ERROR("TSHttpHdrReasonGet returned null string or zero length. str=%p, length=%d, hdr_buf=%p, hdr_loc=%p", str, length,
                state_->hdr_buf_, state_->hdr_loc_);
    }
  }
  return ret_str; // if not initialized, we will just return an empty string
}

void
Response::setReasonPhrase(const string &phrase)
{
  if (state_->hdr_buf_ && state_->hdr_loc_) {
    TSHttpHdrReasonSet(state_->hdr_buf_, state_->hdr_loc_, phrase.c_str(), phrase.length());
    LOG_DEBUG("Changing response reason phrase to '%s' with hdr_buf=%p and hdr_loc=%p", phrase.c_str(), state_->hdr_buf_,
              state_->hdr_loc_);
  }
}

Headers &
Response::getHeaders() const
{
  return state_->headers_; // if not initialized, we will just return an empty object
}

Response::~Response()
{
  delete state_;
}
