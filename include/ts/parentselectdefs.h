/** @file

  Traffic Server SDK API header file

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

  @section developers Developers

  NextHop plugin interface.

 */

#pragma once

#include <string.h>
#include <netinet/in.h>

// Plugins may set this to indicate how to retry.
//
// If handled is false, then no plugin set it, and Core will proceed to do its own thing.
//
// If handled is true, core will not do any parent processing, markdown, or anything else,
// but will use the values in this for whether to use the existing response or make another request,
// and what that request should look like.
//
// See the API functions which take this for ownership requirements of pointers, like hostname.
//
// hostname is the hostname to use for the next request. It must be null-terminated.
// hostname_len is the length of hostname, not including the terminating null.
//
typedef struct {
  // TODO this shouldn't be necessary - plugins should manipulate the response as they see fit,
  // core shouldn't "know" if it was a "success" or "failure," only the response or retry data/action.
  // But for now, core needs to know, for reasons.
  const char *hostname;
  size_t hostname_len;
  in_port_t port;
  bool fail;
  bool is_retry;
  bool nextHopExists;
  bool responseIsRetryable;
  bool goDirect;
  bool parentIsProxy;
  bool no_cache;
} TSResponseAction;
