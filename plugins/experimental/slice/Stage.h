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

#pragma once

#include "ts/ts.h"

#include "slice.h"
#include "util.h"

#include <cinttypes>

struct Channel {
  TSVIO m_vio{nullptr};
  TSIOBuffer m_iobuf{nullptr};
  TSIOBufferReader m_reader{nullptr};

  ~Channel()
  {
    if (nullptr != m_reader) {
      TSIOBufferReaderFree(m_reader);
#if defined(COLLECT_STATS)
      TSStatIntDecrement(stats::Reader, 1);
#endif
    }
    if (nullptr != m_iobuf) {
      TSIOBufferDestroy(m_iobuf);
    }
  }

  int64_t
  drainReader()
  {
    int64_t consumed = 0;

    if (nullptr != m_reader && reader_avail_more_than(m_reader, 0)) {
      int64_t const avail = TSIOBufferReaderAvail(m_reader);
      TSIOBufferReaderConsume(m_reader, avail);
      consumed = avail;
      TSVIONDoneSet(m_vio, TSVIONDoneGet(m_vio) + consumed);
    }

    return consumed;
  }

  bool
  setForRead(TSVConn vc, TSCont contp, int64_t const bytesin)
  {
    TSAssert(nullptr != vc);
    if (nullptr == m_iobuf) {
      m_iobuf  = TSIOBufferCreate();
      m_reader = TSIOBufferReaderAlloc(m_iobuf);
#if defined(COLLECT_STATS)
      TSStatIntIncrement(stats::Reader, 1);
#endif
    } else {
      int64_t const drained = drainReader();
      if (0 < drained) {
        DEBUG_LOG("Drained from reader: %" PRId64, drained);
      }
    }
    m_vio = TSVConnRead(vc, contp, m_iobuf, bytesin);
    return nullptr != m_vio;
  }

  bool
  setForWrite(TSVConn vc, TSCont contp, int64_t const bytesout)
  {
    TSAssert(nullptr != vc);
    if (nullptr == m_iobuf) {
      m_iobuf  = TSIOBufferCreate();
      m_reader = TSIOBufferReaderAlloc(m_iobuf);
#if defined(COLLECT_STATS)
      TSStatIntIncrement(stats::Reader, 1);
#endif
    } else {
      int64_t const drained = drainReader();
      if (0 < drained) {
        DEBUG_LOG("Drained from reader: %" PRId64, drained);
      }
    }
    m_vio = TSVConnWrite(vc, contp, m_reader, bytesout);
    return nullptr != m_vio;
  }

  void
  close()
  {
    if (nullptr != m_reader) {
      drainReader();
    }
    m_vio = nullptr;
  }

  bool
  isOpen() const
  {
    return nullptr != m_vio;
  }

  bool
  isDrained() const
  {
    return nullptr == m_reader || !reader_avail_more_than(m_reader, 0);
  }
};

struct Stage // upstream or downstream (server or client)
{
  Stage(Stage const &) = delete;
  Stage &operator=(Stage const &) = delete;

  TSVConn m_vc{nullptr};
  Channel m_read;
  Channel m_write;

  Stage() {}
  ~Stage()
  {
    if (nullptr != m_vc) {
      TSVConnClose(m_vc);
    }
  }

  void
  setupConnection(TSVConn vc)
  {
    if (nullptr != m_vc) {
      TSVConnClose(m_vc);
    }
    m_read.close();
    m_write.close();
    m_vc = vc;
  }

  void
  setupVioRead(TSCont contp, int64_t const bytesin)
  {
    m_read.setForRead(m_vc, contp, bytesin);
  }

  void
  setupVioWrite(TSCont contp, int64_t const bytesout)
  {
    m_write.setForWrite(m_vc, contp, bytesout);
  }

  void
  abort()
  {
    if (nullptr != m_vc) {
      TSVConnAbort(m_vc, TS_VC_CLOSE_ABORT);
      m_vc = nullptr;
    }
    m_read.close();
    m_write.close();
  }

  void
  close()
  {
    if (nullptr != m_vc) {
      TSVConnClose(m_vc);
      m_vc = nullptr;
    }
    m_read.close();
    m_write.close();
  }

  bool
  isOpen() const
  {
    return nullptr != m_vc && (m_read.isOpen() || m_write.isOpen());
  }
};
