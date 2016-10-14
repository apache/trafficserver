/*
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

#include "ts_lua_io.h"

int64_t
IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length)
{
  int64_t avail, need, n;
  const char *start;
  TSIOBufferBlock blk;

  n   = 0;
  blk = TSIOBufferReaderStart(readerp);

  while (blk) {
    start = TSIOBufferBlockReadStart(blk, readerp, &avail);
    need  = length < avail ? length : avail;

    if (need > 0) {
      memcpy((char *)buf + n, start, need);
      length -= need;
      n += need;
    }

    if (length == 0) {
      break;
    }

    blk = TSIOBufferBlockNext(blk);
  }

  return n;
}
