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

int64_t
transfer_content_bytes(Data *const data)
{
  // nothing to transfer if there's no source.
  if (nullptr == data->m_upstream.m_read.m_reader) {
    return 0;
  }

  TSIOBufferReader const reader = data->m_upstream.m_read.m_reader;
  TSIOBuffer const output_buf   = data->m_dnstream.m_write.m_iobuf;
  TSVIO const output_vio        = data->m_dnstream.m_write.m_vio;

  int64_t consumed = 0; // input vio bytes visited
  int64_t copied   = 0; // output bytes transferred

  int64_t avail = TSIOBufferReaderAvail(reader);
  if (0 < avail) {
    int64_t const toskip = std::min(data->m_blockskip, avail);
    if (0 < toskip) {
      TSIOBufferReaderConsume(reader, toskip);
      data->m_blockskip -= toskip;
      avail -= toskip;
      consumed += toskip;
    }
  }

  // bool const canWrite = data->m_dnstream.m_write.isOpen();

  if (0 < avail) {
    int64_t const bytesleft = data->m_bytestosend - data->m_bytessent;
    int64_t const tocopy    = std::min(avail, bytesleft);
    if (0 < tocopy) {
      copied = TSIOBufferCopy(output_buf, reader, tocopy, 0);

      data->m_bytessent += copied;
      TSIOBufferReaderConsume(reader, copied);

      avail -= copied;
      consumed += copied;
    }
  }

  // if hit fulfillment start bulk consuming
  if (0 < avail && data->m_bytestosend <= data->m_bytessent) {
    TSIOBufferReaderConsume(reader, avail);
    consumed += avail;
  }

  if (0 < copied && nullptr != output_vio) {
    TSVIOReenable(output_vio);
  }

  if (0 < consumed) {
    data->m_blockconsumed += consumed;

    TSVIO const input_vio = data->m_upstream.m_read.m_vio;
    if (nullptr != input_vio) {
      TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + consumed);
    }
  }

  return consumed;
}

// transfer all bytes from the server (error condition)
int64_t
transfer_all_bytes(Data *const data)
{
  // nothing to transfer if there's no source.
  if (nullptr == data->m_upstream.m_read.m_reader || !data->m_dnstream.m_write.isOpen()) {
    return 0;
  }

  int64_t consumed = 0; // input vio bytes visited

  TSIOBufferReader const reader = data->m_upstream.m_read.m_reader;
  TSIOBuffer const output_buf   = data->m_dnstream.m_write.m_iobuf;

  int64_t const read_avail = TSIOBufferReaderAvail(reader);

  if (0 < read_avail) {
    int64_t const copied = TSIOBufferCopy(output_buf, reader, read_avail, 0);

    if (0 < copied) {
      TSIOBufferReaderConsume(reader, copied);
      consumed = copied;

      TSVIO const output_vio = data->m_dnstream.m_write.m_vio;
      if (nullptr != output_vio) {
        TSVIOReenable(output_vio);
      }
    }
  }

  if (0 < consumed) {
    TSVIO const input_vio = data->m_upstream.m_read.m_vio;
    if (nullptr != input_vio) {
      TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + consumed);
    }
  }

  return consumed;
}
