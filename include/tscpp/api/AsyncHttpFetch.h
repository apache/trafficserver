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
 * @file AsyncHttpFetch.h
 */

#pragma once

#include <string>
#include <memory>
#include "tscpp/api/Async.h"
#include "tscpp/api/Request.h"
#include "tscpp/api/Response.h"

namespace atscppapi
{
// forward declarations
struct AsyncHttpFetchState;
namespace utils
{
  class internal;
}

/**
 * @brief This class provides an implementation of AsyncProvider that
 * makes HTTP requests asynchronously. This provider automatically
 * self-destructs after the completion of the request.
 *
 * See example async_http_fetch{,_streaming} for sample usage.
 */
class AsyncHttpFetch : public AsyncProvider
{
public:
  /** Deprecated. Use variant with streaming flag argument */
  AsyncHttpFetch(const std::string &url_str, HttpMethod http_method = HTTP_METHOD_GET);

  /** Deprecated. Use variant with streaming flag argument */
  AsyncHttpFetch(const std::string &url_str, const std::string &request_body);

  enum StreamingFlag {
    STREAMING_DISABLED = 0,
    STREAMING_ENABLED  = 0x1,
  };

  AsyncHttpFetch(const std::string &url_str, StreamingFlag streaming_flag, HttpMethod http_method = HTTP_METHOD_GET);

  AsyncHttpFetch(const std::string &url_str, StreamingFlag streaming_flag, const std::string &request_body);

  /**
   * Used to manipulate the headers of the request to be made.
   *
   * @return A reference to mutable headers.
   */
  Headers &getRequestHeaders();

  enum Result {
    RESULT_SUCCESS = 10000,
    RESULT_TIMEOUT,
    RESULT_FAILURE,
    RESULT_HEADER_COMPLETE,
    RESULT_PARTIAL_BODY,
    RESULT_BODY_COMPLETE
  };

  /**
   * Used to extract the response after request completion. Without
   * streaming, this can result success, failure or timeout. With
   * streaming, this can result failure, timeout, header complete,
   * partial body or body complete.
   *
   * @return Result of the operation
   */
  Result getResult() const;

  /**
   * @return Non-mutable reference to the request URL.
   */
  const Url &getRequestUrl() const;

  /**
   * @return Non-mutable reference to the request body.
   */
  const std::string &getRequestBody() const;

  /**
   * Used to extract the response after request completion (after
   * RESULT_HEADER_COMPLETE in case of streaming).
   *
   * @return Non-mutable reference to the response.
   */
  const Response &getResponse() const;

  /**
   * Used to extract the body of the response after request completion. On
   * unsuccessful completion, values (nullptr, 0) are set.
   *
   * When streaming is enabled, this can be called on either body result.
   *
   * @param body Output argument; will point to the body
   * @param body_size Output argument; will contain the size of the body
   *
   */
  void getResponseBody(const void *&body, size_t &body_size) const;

  /**
   * Starts a HTTP fetch of the Request contained.
   */
  void run() override;

protected:
  ~AsyncHttpFetch() override;

private:
  AsyncHttpFetchState *state_;
  void init(const std::string &url_str, HttpMethod http_method, const std::string &request_body, StreamingFlag streaming_flag);
  friend class utils::internal;
};

} // namespace atscppapi
