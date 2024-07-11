/** @file

  Operations on cache documents (may also be called fragments).

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

#include "P_CacheDoc.h"

#include "iocore/eventsystem/IOBuffer.h"

#include "tscore/ink_hrtime.h"

#include <cstring>

namespace
{

char *
iobufferblock_memcpy(char *p, int len, IOBufferBlock const *ab, int offset)
{
  IOBufferBlock const *b = ab;
  while (b && len >= 0) {
    char *start      = b->_start;
    char *end        = b->_end;
    int   max_bytes  = end - start;
    max_bytes       -= offset;
    if (max_bytes <= 0) {
      offset = -max_bytes;
      b      = b->next.get();
      continue;
    }
    int bytes = len;
    if (bytes >= max_bytes) {
      bytes = max_bytes;
    }
    ::memcpy(p, start + offset, bytes);
    p      += bytes;
    len    -= bytes;
    b       = b->next.get();
    offset  = 0;
  }
  return p;
}

} // namespace

void
Doc::set_data(int const len, IOBufferBlock const *block, int const offset)
{
  iobufferblock_memcpy(this->data(), len, block, offset);
}

void
Doc::calculate_checksum()
{
  this->checksum = 0;
  for (char *b = this->hdr(); b < reinterpret_cast<char *>(this) + this->len; b++) {
    this->checksum += *b;
  }
}

void
Doc::pin(std::uint32_t const pin_in_cache)
{
  // coverity[Y2K38_SAFETY:FALSE]
  this->pinned = static_cast<uint32_t>(ink_get_hrtime() / HRTIME_SECOND) + pin_in_cache;
}

void
Doc::unpin()
{
  this->pinned = 0;
}
