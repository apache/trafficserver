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
#include <cstring>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <ts/ts.h>

namespace ats
{
namespace io
{
  struct IO {
    TSIOBuffer buffer;
    TSIOBufferReader reader;
    TSVIO vio;

    ~IO()
    {
      assert(buffer != nullptr);
      assert(reader != nullptr);
      const int64_t available = TSIOBufferReaderAvail(reader);
      if (available > 0) {
        TSIOBufferReaderConsume(reader, available);
      }
      TSIOBufferReaderFree(reader);
      TSIOBufferDestroy(buffer);
    }

    IO() : buffer(TSIOBufferCreate()), reader(TSIOBufferReaderAlloc(buffer)), vio(nullptr) {}
    IO(const TSIOBuffer &b) : buffer(b), reader(TSIOBufferReaderAlloc(buffer)), vio(nullptr) { assert(buffer != nullptr); }
    static IO *read(TSVConn, TSCont, const int64_t);

    static IO *
    read(TSVConn v, TSCont c)
    {
      return IO::read(v, c, std::numeric_limits<int64_t>::max());
    }
  };

} // namespace io
} // namespace ats
