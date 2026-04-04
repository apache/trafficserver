/** @file

  Shared TLS ClientHello summary for JA3-family fingerprints.

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

#pragma once

#include "ja3_model.h"
#include "ts/ts.h"

namespace ja3
{

/**
 * Fetch or build the cached TLS ClientHello summary for a connection.
 *
 * The summary is parsed once per connection and then shared by the JA3, JAWS
 * v1, and JAWS v2 method implementations.
 *
 * @param[in] vconn Connection whose ClientHello should be summarized.
 * @return Pointer to the cached summary owned by the module, or @c nullptr if
 * the ClientHello could not be obtained.
 */
ClientHelloSummary const *get_or_create_client_hello_summary(TSVConn vconn);

/**
 * Drop the cached TLS ClientHello summary for a connection.
 *
 * This releases the per-connection summary once the owning vconn is closing.
 *
 * @param[in] vconn Connection whose cached summary should be removed.
 * @return None.
 */
void clear_cached_client_hello_summary(TSVConn vconn);

} // namespace ja3
