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
#ifndef ATSCPPAPI_ASYNCHTTPFETCH_H_
#define ATSCPPAPI_ASYNCHTTPFETCH_H_

#include <string>
#include <atscppapi/shared_ptr.h>
#include <atscppapi/Async.h>
#include <atscppapi/Request.h>
#include <atscppapi/Response.h>

namespace atscppapi {

// forward declarations
struct AsyncHttpFetchState;
namespace utils { class internal; }

/**
 * @brief This class provides an implementation of AsyncProvider that
 * makes HTTP requests asynchronously. This provider automatically
 * self-destructs after the completion of the request.
 *
 * See example async_http_fetch for sample usage.
 */
class AsyncHttpFetch : public AsyncProvider {
public:
  AsyncHttpFetch(const std::string &url_str, HttpMethod http_method = HTTP_METHOD_GET);

  AsyncHttpFetch(const std::string &url_str,  const std::string &request_body);

  /**
   * Used to manipulate the headers of the request to be made.
   *
   * @return A reference to mutable headers.
   */
  Headers &getRequestHeaders();

  enum Result { RESULT_SUCCESS = 10000, RESULT_TIMEOUT, RESULT_FAILURE };

  /**
   * Used to extract the response after request completion. 
   *
   * @return Result of the operation
   */
  Result getResult() const;

  /**
   * @return Non-mutable reference to the request URL.
   */
  const Url &getRequestUrl() const;

  /**
   * Used to extract the response after request completion. 
   *
   * @return Non-mutable reference to the response.
   */
  const Response &getResponse() const;

  /**
   * Used to extract the body of the response after request completion. On
   * unsuccessful completion, values (NULL, 0) are set.
   *
   * @param body Output argument; will point to the body
   * @param body_size Output argument; will contain the size of the body 
   * 
   */
  void getResponseBody(const void *&body, size_t &body_size) const;

  /**
   * Starts a HTTP fetch of the Request contained.
   */  
  virtual void run();
protected:
  virtual ~AsyncHttpFetch();

private:
  AsyncHttpFetchState *state_;
  void init(const std::string &url_str, HttpMethod http_method, const std::string &request_body);
  friend class utils::internal;
};

} /* atscppapi */

#endif /* ATSCPPAPI_ASYNCHTTPFETCH_H_ */
