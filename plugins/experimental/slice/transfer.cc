/** @file
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

#include "transfer.h"

int64_t transfer_content_bytes(Data *const data) // , char const * const fstr)
{
  int64_t consumed(0);

  // is the downstream is fulfilled or closed
  if (!data->m_dnstream.m_write.isOpen()) {
    // drain the upstream
    if (data->m_upstream.m_read.isOpen()) {
      int64_t const avail = TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader);
      TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, avail);
      consumed += avail;
    }
  } else // if (data->m_dnstream.m_write.isOpen())
  {
    if (data->m_upstream.m_read.isOpen()) {
      int64_t avail = TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader);
      if (0 < avail) {
        int64_t const toskip = std::min(data->m_blockskip, avail);

        // consume any up front (first block) padding
        if (0 < toskip) {
          TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, toskip);
          data->m_blockskip -= toskip;
          avail -= toskip;
          consumed += toskip;
        }

        if (0 < avail) {
          int64_t const bytesleft = (data->m_bytestosend - data->m_bytessent);
          int64_t const tocopy    = std::min(avail, bytesleft);

          if (0 < tocopy) {
            int64_t const copied(TSIOBufferCopy(data->m_dnstream.m_write.m_iobuf, data->m_upstream.m_read.m_reader, tocopy, 0));

            data->m_bytessent += copied;

            TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);

            avail -= copied;
            consumed += copied;
          }
        }

        // if hit fulfillment start bulk consuming
        if (0 < avail && data->m_bytestosend <= data->m_bytessent) {
          TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, avail);
          consumed += avail;
        }
      }

      if (0 < consumed) {
        TSVIOReenable(data->m_dnstream.m_write.m_vio);
      }
    }
  }

  if (0 < consumed) {
    data->m_blockconsumed += consumed;
  }

  return consumed;
}

// transfer all bytes from the server (error condition)
int64_t
transfer_all_bytes(Data *const data)
{
  DEBUG_LOG("transfer_all_bytes");
  int64_t consumed = 0;

  if (data->m_dnstream.m_write.isOpen()) {
    int64_t const read_avail = TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader);

    if (0 < read_avail) {
      int64_t const copied(TSIOBufferCopy(data->m_dnstream.m_write.m_iobuf, data->m_upstream.m_read.m_reader, read_avail, 0));

      if (0 < copied) {
        TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);
        consumed = copied;
      }
    }
  }

  return consumed;
}
