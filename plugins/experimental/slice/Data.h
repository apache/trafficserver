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
#include "Stage.h"
#include "Range.h"
#include "slice.h"

#include <netinet/in.h>

struct Data {
  Data(Data const &) = delete;
  Data &operator=(Data const &) = delete;

  int64_t m_blockbytes;
  sockaddr_storage m_client_ip;

  // for pristine url coming in
  TSMBuffer m_urlbuffer{nullptr};
  TSMLoc m_urlloc{nullptr};

  char m_hostname[1024];
  int m_hostlen;

  TSHttpStatus m_statustype; // 200 or 206

  bool m_bail; // non 206/200 response

  Range m_req_range; // converted to half open interval
  int64_t m_contentlen;

  int64_t m_blocknum;  //!< block number to work on, -1 bad/stop
  int64_t m_skipbytes; //!< number of bytes to skip in this block

  int64_t m_bytestosend; //!< header + content bytes to send
  int64_t m_bytessent;   //!< number of content bytes sent

  bool m_server_block_header_parsed;
  bool m_server_first_header_parsed;
  bool m_client_header_sent;

  Stage m_upstream;
  Stage m_dnstream;

  HdrMgr m_req_hdrmgr;  //!< manager for server request
  HdrMgr m_resp_hdrmgr; //!< manager for client response

  TSHttpParser m_http_parser{nullptr}; //!< cached for reuse

  explicit Data(int64_t const blockbytes)
    : m_blockbytes(blockbytes),
      m_client_ip(),
      m_urlbuffer(nullptr),
      m_urlloc(nullptr),
      m_hostlen(0),
      m_statustype(TS_HTTP_STATUS_NONE),
      m_bail(false),
      m_req_range(-1, -1),
      m_contentlen(-1),
      m_blocknum(-1),
      m_skipbytes(0),
      m_bytestosend(0),
      m_bytessent(0),
      m_server_block_header_parsed(false),
      m_server_first_header_parsed(false),
      m_client_header_sent(false),
      m_http_parser(nullptr)
  {
    m_hostname[0] = '\0';
  }

  ~Data()
  {
    if (nullptr != m_urlloc && nullptr != m_urlbuffer) {
      TSHandleMLocRelease(m_urlbuffer, TS_NULL_MLOC, m_urlloc);
    }
    if (nullptr != m_urlbuffer) {
      TSMBufferDestroy(m_urlbuffer);
    }
    if (nullptr != m_http_parser) {
      TSHttpParserDestroy(m_http_parser);
    }
  }
};
