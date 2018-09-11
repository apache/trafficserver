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
 * @file ClientRequest.h
 */

#pragma once

#include "tscpp/api/Request.h"

namespace atscppapi
{
struct ClientRequestState;

/**
 * @brief Encapsulates a client request. A client request is different from a
 * server request as it has two URLs - the pristine URL sent by the client
 * and a remapped URL created by the server.
 */
class ClientRequest : public Request
{
public:
  /**
   * @private
   */
  ClientRequest(void *raw_txn, void *hdr_buf, void *hdr_loc);

  /**
   * Returns the pristine (pre-remap) client request URL
   *
   * @return Url Reference to non-mutable pristine URL.
   */
  const Url &getPristineUrl() const;

  ~ClientRequest();

private:
  ClientRequestState *state_;
};
} // namespace atscppapi
