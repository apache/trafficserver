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
 * @file Request.h
 */

#pragma once

#include "tscpp/api/Headers.h"
#include "tscpp/api/HttpVersion.h"
#include "tscpp/api/HttpMethod.h"
#include "tscpp/api/Url.h"
#include "tscpp/api/noncopyable.h"

namespace atscppapi
{
namespace utils
{
  class internal;
}

class Transaction;
struct RequestState;

/**
 * @brief Encapsulates a request.
 */
class Request : noncopyable
{
public:
  Request();

  /**
   * Constructed with an initial URL.
   */
  Request(const std::string &url, HttpMethod method = HTTP_METHOD_GET, HttpVersion version = HTTP_VERSION_1_1);

  /** @return HTTP method of the request */
  HttpMethod getMethod() const;

  /** @return URL of the request */
  Url &getUrl();

  /** @return HTTP version of the request */
  HttpVersion getVersion() const;

  /** @return Headers of the request */
  Headers &getHeaders() const;

  /** Set the @a host for the request.

      This sets all elements of the request related to the host. A @c
      HOST header will be forced or updated, and the URL updated if it
      already contains a host.

      @note This is not the same as setting the @c HOST header
      explicitly - that can lead to a malformed request where the host
      in the method URL is different from the host in the HOST field.
  */
  void setHost(std::string const &host);

  ~Request();

private:
  Request(void *hdr_buf, void *hdr_loc);
  RequestState *state_;
  void init(void *hdr_buf, void *hdr_loc);
  void reset();
  friend class Transaction;
  friend class ClientRequest;
  friend class utils::internal;
};
} // namespace atscppapi
