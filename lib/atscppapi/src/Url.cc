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
 * @file Url.cc
 */
#include "atscppapi/Url.h"
#include <ts/ts.h>
#include "atscppapi/noncopyable.h"
#include "logging_internal.h"

using namespace atscppapi;
using std::string;

/**
 * @private
 */
struct atscppapi::UrlState : noncopyable {
  TSMBuffer hdr_buf_;
  TSMLoc url_loc_;
  UrlState(TSMBuffer hdr_buf, TSMLoc url_loc) : hdr_buf_(hdr_buf), url_loc_(url_loc) {}
};

Url::Url()
{
  state_ = new UrlState(static_cast<TSMBuffer>(NULL), static_cast<TSMLoc>(NULL));
}

Url::Url(void *hdr_buf, void *url_loc)
{
  state_ = new UrlState(static_cast<TSMBuffer>(hdr_buf), static_cast<TSMLoc>(url_loc));
}

void
Url::init(void *hdr_buf, void *url_loc)
{
  state_->hdr_buf_ = static_cast<TSMBuffer>(hdr_buf);
  state_->url_loc_ = static_cast<TSMLoc>(url_loc);
}

Url::~Url()
{
  delete state_;
}

bool inline Url::isInitialized() const
{
  return state_->hdr_buf_ && state_->url_loc_;
}

void
Url::reset()
{
}

std::string
Url::getUrlString() const
{
  std::string ret_str;
  if (isInitialized()) {
    int length;
    char *memptr = TSUrlStringGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      ret_str.assign(memptr, length);
      TSfree(memptr);
      LOG_DEBUG("Got URL [%s]", ret_str.c_str());
    } else {
      LOG_ERROR("Got null/zero-length URL string; hdr_buf %p, url_loc %p, ptr %p, length %d", state_->hdr_buf_, state_->url_loc_,
                memptr, length);
    }
  }
  return ret_str;
}

std::string
Url::getPath() const
{
  std::string ret_str;
  if (isInitialized()) {
    int length;
    const char *memptr = TSUrlPathGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      ret_str.assign(memptr, length);
    }
    LOG_DEBUG("Using path [%s]", ret_str.c_str());
  }
  return ret_str;
}

std::string
Url::getQuery() const
{
  std::string ret_str;
  if (isInitialized()) {
    int length;
    const char *memptr = TSUrlHttpQueryGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      ret_str.assign(memptr, length);
    }
    LOG_DEBUG("Using query [%s]", ret_str.c_str());
  }
  return ret_str;
}

std::string
Url::getScheme() const
{
  std::string ret_str;
  if (isInitialized()) {
    int length;
    const char *memptr = TSUrlSchemeGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      ret_str.assign(memptr, length);
    }
    LOG_DEBUG("Using scheme [%s]", ret_str.c_str());
  }
  return ret_str;
}

std::string
Url::getHost() const
{
  std::string ret_str;
  if (isInitialized()) {
    int length;
    const char *memptr = TSUrlHostGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      ret_str.assign(memptr, length);
    }
    LOG_DEBUG("Using host [%s]", ret_str.c_str());
  }
  return ret_str;
}

uint16_t
Url::getPort() const
{
  uint16_t ret_val = 0;
  if (isInitialized()) {
    ret_val = static_cast<uint16_t>(TSUrlPortGet(state_->hdr_buf_, state_->url_loc_));
    LOG_DEBUG("Got port %d", ret_val);
  }
  return ret_val;
}

void
Url::setPath(const std::string &path)
{
  if (!isInitialized()) {
    LOG_ERROR("Url %p not initialized", this);
    return;
  }

  if (TSUrlPathSet(state_->hdr_buf_, state_->url_loc_, path.c_str(), path.length()) == TS_SUCCESS) {
    LOG_DEBUG("Set path to [%s]", path.c_str());
  } else {
    LOG_ERROR("Could not set path; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void
Url::setQuery(const std::string &query)
{
  if (!isInitialized()) {
    LOG_ERROR("Url %p not initialized", this);
    return;
  }

  if (TSUrlHttpQuerySet(state_->hdr_buf_, state_->url_loc_, query.c_str(), query.length()) == TS_SUCCESS) {
    LOG_DEBUG("Set query to [%s]", query.c_str());
  } else {
    LOG_ERROR("Could not set query; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void
Url::setScheme(const std::string &scheme)
{
  if (!isInitialized()) {
    LOG_ERROR("Url %p not initialized", this);
    return;
  }

  if (TSUrlSchemeSet(state_->hdr_buf_, state_->url_loc_, scheme.c_str(), scheme.length()) == TS_SUCCESS) {
    LOG_DEBUG("Set scheme to [%s]", scheme.c_str());
  } else {
    LOG_ERROR("Could not set scheme; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void
Url::setHost(const std::string &host)
{
  if (!isInitialized()) {
    LOG_ERROR("Url %p not initialized", this);
    return;
  }

  if (TSUrlHostSet(state_->hdr_buf_, state_->url_loc_, host.c_str(), host.length()) == TS_SUCCESS) {
    LOG_DEBUG("Set host to [%s]", host.c_str());
  } else {
    LOG_ERROR("Could not set host; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void
Url::setPort(const uint16_t port)
{
  if (!isInitialized()) {
    LOG_ERROR("Url %p not initialized", this);
    return;
  }

  if (TSUrlPortSet(state_->hdr_buf_, state_->url_loc_, port) == TS_SUCCESS) {
    LOG_DEBUG("Set port to %d", port);
  } else {
    LOG_ERROR("Could not set port; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}
