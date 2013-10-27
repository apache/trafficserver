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
#include "InitializableValue.h"
#include "logging_internal.h"

using namespace atscppapi;
using std::string;

/**
 * @private
 */
struct atscppapi::UrlState: noncopyable {
  TSMBuffer hdr_buf_;
  TSMLoc url_loc_;
  InitializableValue<string> url_string_;
  InitializableValue<string> path_;
  InitializableValue<string> query_;
  InitializableValue<string> host_;
  InitializableValue<string> scheme_;
  InitializableValue<uint16_t> port_;
  UrlState(TSMBuffer hdr_buf, TSMLoc url_loc) :
      hdr_buf_(hdr_buf), url_loc_(url_loc) {
  }
};

Url::Url() {
  state_ = new UrlState(static_cast<TSMBuffer>(NULL), static_cast<TSMLoc>(NULL));
}

Url::Url(void *hdr_buf, void *url_loc) {
  state_ = new UrlState(static_cast<TSMBuffer>(hdr_buf), static_cast<TSMLoc>(url_loc));
}

void Url::init(void *hdr_buf, void *url_loc) {
  state_->hdr_buf_ = static_cast<TSMBuffer>(hdr_buf);
  state_->url_loc_ = static_cast<TSMLoc>(url_loc);
}

Url::~Url() {
  delete state_;
}

bool inline Url::isInitialized() const {
  return state_->hdr_buf_ && state_->url_loc_;
}

void Url::reset() {
  state_->url_string_.setInitialized(false);
  state_->path_.setInitialized(false);
  state_->query_.setInitialized(false);
  state_->host_.setInitialized(false);
  state_->scheme_.setInitialized(false);
  state_->port_.setInitialized(false);
}

const std::string &Url::getUrlString() const {
  if (isInitialized() && !state_->url_string_.isInitialized()) {
    int length;
    char *memptr = TSUrlStringGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      state_->url_string_ = std::string(memptr, length);
      TSfree(memptr);
      LOG_DEBUG("Got URL [%s]", state_->url_string_.getValue().c_str());
    } else {
      LOG_ERROR("Got null/zero-length URL string; hdr_buf %p, url_loc %p, ptr %p, length %d", state_->hdr_buf_,
                state_->url_loc_, memptr, length);
    }
  }
  return state_->url_string_;
}

const std::string &Url::getPath() const {
  if (isInitialized() && !state_->path_.isInitialized()) {
    int length;
    const char *memptr = TSUrlPathGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      state_->path_ = std::string(memptr, length);
    }
    LOG_DEBUG("Using path [%s]", state_->path_.getValue().c_str());
  }
  return state_->path_;
}

const std::string &Url::getQuery() const {
  if (isInitialized() && !state_->query_.isInitialized()) {
    int length;
    const char *memptr = TSUrlHttpQueryGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      state_->query_ = std::string(memptr, length);
    }
    LOG_DEBUG("Using query [%s]", state_->query_.getValue().c_str());
  }
  return state_->query_;
}

const std::string &Url::getScheme() const {
  if (isInitialized() && !state_->scheme_.isInitialized()) {
    int length;
    const char *memptr = TSUrlSchemeGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      state_->scheme_ = std::string(memptr, length);
    }
    LOG_DEBUG("Using scheme [%s]", state_->scheme_.getValue().c_str());
  }
  return state_->scheme_;
}

const std::string &Url::getHost() const {
  if (isInitialized() && !state_->host_.isInitialized()) {
    int length;
    const char *memptr = TSUrlHostGet(state_->hdr_buf_, state_->url_loc_, &length);
    if (memptr && length) {
      state_->host_ = std::string(memptr, length);
    }
    LOG_DEBUG("Using host [%s]", state_->host_.getValue().c_str());
  }
  return state_->host_;
}

uint16_t Url::getPort() const {
  if (isInitialized() && !state_->port_.isInitialized()) {
    state_->port_ = TSUrlPortGet(state_->hdr_buf_, state_->url_loc_);
    LOG_DEBUG("Got port %d", state_->port_.getValue());
  }
  return state_->port_;
}

void Url::setPath(const std::string &path) {
  if (!isInitialized()) {
    LOG_ERROR("Not initialized");
    return;
  }
  state_->url_string_.setInitialized(false);
  if (TSUrlPathSet(state_->hdr_buf_, state_->url_loc_, path.c_str(), path.length()) == TS_SUCCESS) {
    state_->path_ = path;
    LOG_DEBUG("Set path to [%s]", path.c_str());
  } else {
    LOG_ERROR("Could not set path; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void Url::setQuery(const std::string &query) {
  if (!isInitialized()) {
    LOG_ERROR("Not initialized");
    return;
  }
  state_->url_string_.setInitialized(false);
  if (TSUrlHttpQuerySet(state_->hdr_buf_, state_->url_loc_, query.c_str(), query.length()) == TS_SUCCESS) {
    state_->query_ = query;
    LOG_DEBUG("Set query to [%s]", query.c_str());
  } else {
    LOG_ERROR("Could not set query; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void Url::setScheme(const std::string &scheme) {
  if (!isInitialized()) {
    LOG_ERROR("Not initialized");
    return;
  }
  state_->url_string_.setInitialized(false);
  if (TSUrlSchemeSet(state_->hdr_buf_, state_->url_loc_, scheme.c_str(), scheme.length()) == TS_SUCCESS) {
    state_->scheme_ = scheme;
    LOG_DEBUG("Set scheme to [%s]", scheme.c_str());
  } else {
    LOG_ERROR("Could not set scheme; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void Url::setHost(const std::string &host) {
  if (!isInitialized()) {
    LOG_ERROR("Not initialized");
    return;
  }
  state_->url_string_.setInitialized(false);
  if (TSUrlHostSet(state_->hdr_buf_, state_->url_loc_, host.c_str(), host.length()) == TS_SUCCESS) {
    state_->host_ = host;
    LOG_DEBUG("Set host to [%s]", host.c_str());
  } else {
    LOG_ERROR("Could not set host; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}

void Url::setPort(const uint16_t port) {
  if (!isInitialized()) {
    LOG_ERROR("Not initialized");
    return;
  }
  state_->url_string_.setInitialized(false);
  if (TSUrlPortSet(state_->hdr_buf_, state_->url_loc_, port) == TS_SUCCESS) {
    state_->port_ = port;
    LOG_DEBUG("Set port to %d", port);
  } else {
    LOG_ERROR("Could not set port; hdr_buf %p, url_loc %p", state_->hdr_buf_, state_->url_loc_);
  }
}
