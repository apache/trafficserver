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

#pragma once

#include <cassert>
#include <memory>
#include <string>
#include <ts/ts.h>
#include <vector>

#include "ts.h"

#ifdef __OPTIMIZE__

// Optimized -- release build.

// For CHECK(), execute any side effects (only) for expression X.
#define CHECK(X)          \
  {                       \
    static_cast<void>(X); \
  }

// Make sure assert() disabled.
#ifndef NDEBUG
#define NDEBUG
#endif

#else

// Check if expression X returns a value that implicitly converts to bool false (such as TS_SUCCESS).
#define CHECK(X)                \
  {                             \
    static_assert(!TS_SUCCESS); \
    assert(!(X));               \
  }

#endif

struct Statistics {
  int failures;
  int hits;
  int time; // average
  int requests;
  int timeouts;
  int size; // average
};

typedef std::vector<std::string> Origins;

struct Request {
  std::string host;
  int length;
  std::unique_ptr<ats::io::IO> io;

  Request(const std::string &, const TSMBuffer, const TSMLoc);
  Request(const Request &) = delete;
  Request(Request &&);
  Request &operator=(const Request &);
};

typedef std::vector<Request> Requests;

struct Instance {
  Origins origins;
  bool skipPostPut;
};

extern size_t timeout;

void generateRequests(const Origins &, const TSMBuffer, const TSMLoc, Requests &);
void addBody(Requests &, const TSIOBufferReader);
void dispatch(Requests &, const int timeout = 0);
