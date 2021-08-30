/** @file

  Http2CommonSessionInternal.

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
#include "Http2CommonSession.h"

#define REMEMBER(e, r)                          \
  {                                             \
    this->remember(MakeSourceLocation(), e, r); \
  }

#define STATE_ENTER(state_name, event)                                                       \
  do {                                                                                       \
    REMEMBER(event, this->recursion)                                                         \
    SsnDebug(this, "http2_cs", "[%" PRId64 "] [%s, %s]", this->connection_id(), #state_name, \
             HttpDebugNames::get_event_name(event));                                         \
  } while (0)

#define Http2SsnDebug(fmt, ...) SsnDebug(this, "http2_cs", "[%" PRId64 "] " fmt, this->connection_id(), ##__VA_ARGS__)

#define HTTP2_SET_SESSION_HANDLER(handler) \
  do {                                     \
    REMEMBER(NO_EVENT, this->recursion);   \
    this->session_handler = (handler);     \
  } while (0)
