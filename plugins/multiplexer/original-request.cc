/** @file

  Multiplexes request to other origins.

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
#include <cstring>

#include "dispatch.h"
#include "original-request.h"

template <class T>
std::string
get(const TSMBuffer &b, const TSMLoc &l, const T &t)
{
  int length               = 0;
  const char *const buffer = t(b, l, &length);

  assert(buffer != nullptr);
  assert(length > 0);

  return std::string(buffer, length);
}

std::string
get(const TSMBuffer &b, const TSMLoc &l, const TSMLoc &f, const int i = 0)
{
  int length               = 0;
  const char *const buffer = TSMimeHdrFieldValueStringGet(b, l, f, i, &length);

  assert(buffer != nullptr);
  assert(length > 0);

  return std::string(buffer, length);
}

OriginalRequest::OriginalRequest(const TSMBuffer b, const TSMLoc l) : buffer_(b), location_(l)
{
  CHECK(TSHttpHdrUrlGet(b, l, &url_));

  assert(url_ != nullptr);

  const_cast<std::string &>(original.urlScheme) = get(buffer_, url_, TSUrlSchemeGet);
  const_cast<std::string &>(original.urlHost)   = get(buffer_, url_, TSUrlHostGet);
  // TODO(dmorilha): handle port

  /*
   * this code assumes the request has a single Host header
   */
  hostHeader_ = TSMimeHdrFieldFind(b, l, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);
  assert(hostHeader_ != nullptr);

  const_cast<std::string &>(original.hostHeader) = get(buffer_, location_, hostHeader_);

  xMultiplexerHeader_ = TSMimeHdrFieldFind(b, l, "X-Multiplexer", 13);

  if (xMultiplexerHeader_ != nullptr) {
    const_cast<std::string &>(original.xMultiplexerHeader) = get(buffer_, location_, xMultiplexerHeader_);
  }
}

OriginalRequest::~OriginalRequest()
{
  urlScheme(original.urlScheme);
  urlHost(original.urlHost);
  hostHeader(original.hostHeader);
  if (!original.xMultiplexerHeader.empty()) {
    xMultiplexerHeader(original.xMultiplexerHeader);
  }

  TSHandleMLocRelease(buffer_, location_, hostHeader_);
  TSHandleMLocRelease(buffer_, location_, url_);
}

void
OriginalRequest::urlScheme(const std::string &s)
{
  assert(buffer_ != nullptr);
  assert(url_ != nullptr);
  CHECK(TSUrlSchemeSet(buffer_, url_, s.c_str(), s.size()));
}

void
OriginalRequest::urlHost(const std::string &s)
{
  assert(buffer_ != nullptr);
  assert(url_ != nullptr);
  CHECK(TSUrlHostSet(buffer_, url_, s.c_str(), s.size()));
}

void
OriginalRequest::hostHeader(const std::string &s)
{
  assert(buffer_ != nullptr);
  assert(location_ != nullptr);
  assert(hostHeader_ != nullptr);
  CHECK(TSMimeHdrFieldValueStringSet(buffer_, location_, hostHeader_, 0, s.c_str(), s.size()));
}

bool
OriginalRequest::xMultiplexerHeader(const std::string &s)
{
  assert(buffer_ != nullptr);
  assert(location_ != nullptr);
  if (xMultiplexerHeader_ == nullptr) {
    return false;
  }
  CHECK(TSMimeHdrFieldValueStringSet(buffer_, location_, xMultiplexerHeader_, 0, s.c_str(), s.size()));
  return true;
}
