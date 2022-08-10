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

#include "HttpHeader.h"
#include "Range.h"
#include "Stage.h"

#include <netinet/in.h>
#include <unordered_map>

struct Config;

enum BlockState {
  Pending,
  PendingInt, // Pending internal refectch
  PendingRef, // Pending reference refetch
  Active,
  ActiveInt, // Active internal refetch
  ActiveRef, // Active reference refetch
  Done,
  Passthru, // non 206 response passthru
  Fail,
};

struct Data {
  Data(Data const &) = delete;
  Data &operator=(Data const &) = delete;

  Config *const m_config;

  sockaddr_storage m_client_ip;

  // transaction pointer
  TSHttpTxn m_txnp{nullptr};

  // for pristine/effective url coming in
  TSMBuffer m_urlbuf{nullptr};
  TSMLoc m_urlloc{nullptr};

  char m_hostname[8192];
  int m_hostlen{0};

  // read from slice block 0
  char m_date[33];
  int m_datelen{0};
  char m_etag[8192];
  int m_etaglen{0};
  char m_lastmodified[33];
  int m_lastmodifiedlen{0};

  int64_t m_contentlen{-1};

  TSHttpStatus m_statustype{TS_HTTP_STATUS_NONE}; // 200 or 206

  Range m_req_range; // converted to half open interval

  int64_t m_blocknum{-1};     // block number to work on, -1 bad/stop
  int64_t m_blockexpected{0}; // body bytes expected
  int64_t m_blockskip{0};     // number of bytes to skip in this block
  int64_t m_blockconsumed{0}; // body bytes consumed

  BlockState m_blockstate{Pending}; // is there an active slice block

  int64_t m_bytestosend{0}; // header + content bytes to send
  int64_t m_bytessent{0};   // number of bytes written to the client

  // default buffer size and water mark
  TSIOBufferSizeIndex m_buffer_index{TS_IOBUFFER_SIZE_INDEX_32K};
  TSIOBufferWaterMark m_buffer_water_mark{TS_IOBUFFER_WATER_MARK_PLUGIN_VC_DEFAULT};

  bool m_server_block_header_parsed{false};
  bool m_server_first_header_parsed{false};

  Stage m_upstream;
  Stage m_dnstream;

  bool m_prefetchable{false};

  HdrMgr m_req_hdrmgr;  // manager for server request
  HdrMgr m_resp_hdrmgr; // manager for client response

  TSHttpParser m_http_parser{nullptr}; //!< cached for reuse

  explicit Data(Config *const config) : m_config(config)
  {
    m_date[0]         = '\0';
    m_hostname[0]     = '\0';
    m_etag[0]         = '\0';
    m_lastmodified[0] = '\0';
  }

  ~Data()
  {
    if (nullptr != m_urlbuf) {
      if (nullptr != m_urlloc) {
        TSHandleMLocRelease(m_urlbuf, TS_NULL_MLOC, m_urlloc);
        m_urlloc = nullptr;
      }
      TSMBufferDestroy(m_urlbuf);
      m_urlbuf = nullptr;
    }
    if (nullptr != m_http_parser) {
      TSHttpParserDestroy(m_http_parser);
      m_http_parser = nullptr;
    }
  }
};
