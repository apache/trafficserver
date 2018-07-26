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
#include "slice.h"

#include <iostream>
#include <netinet/in.h>

void incrData();

void decrData();

struct Data {
  Data(Data const &) = delete;
  Data &operator=(Data const &) = delete;

  int64_t const m_blockbytes_config; // configured slice block size
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

  int64_t m_blocknum;      // block number to work on, -1 bad/stop
  int64_t m_blockexpected; // body bytes expected
  int64_t m_blockskip;     // number of bytes to skip in this block
  int64_t m_blockconsumed; // body bytes consumed
  bool m_iseos;            // server in EOS state

  //	int64_t m_lastconsumed; // debug for last transfer_conten_bytes call
  //	char const * m_fstr;

  int64_t m_bytestosend; // header + content bytes to send
  int64_t m_bytessent;   // number of bytes written to the client

  bool m_server_block_header_parsed;
  bool m_server_first_header_parsed;

  Stage m_upstream;
  Stage m_dnstream;

  HdrMgr m_req_hdrmgr;  // manager for server request
  HdrMgr m_resp_hdrmgr; // manager for client response

  TSHttpParser m_http_parser{nullptr}; //!< cached for reuse

  explicit Data(int64_t const blockbytes)
    : m_blockbytes_config(blockbytes),
      m_client_ip(),
      m_urlbuffer(nullptr),
      m_urlloc(nullptr),
      m_hostlen(0),
      m_statustype(TS_HTTP_STATUS_NONE),
      m_bail(false),
      m_req_range(-1, -1),
      m_contentlen(-1)

      ,
      m_blocknum(-1),
      m_blockexpected(0),
      m_blockskip(0),
      m_blockconsumed(0),
      m_iseos(false)
      //		, m_lastconsumed(0)
      //		, m_fstr(nullptr)

      ,
      m_bytestosend(0),
      m_bytessent(0),
      m_server_block_header_parsed(false),
      m_server_first_header_parsed(false),
      m_http_parser(nullptr)
  {
    //		incrData();
    m_hostname[0] = '\0';
  }

  ~Data()
  {
    //		decrData();
    // std::cerr << "~Data" << std::endl;
    if (nullptr != m_urlbuffer) {
      if (nullptr != m_urlloc) {
        TSHandleMLocRelease(m_urlbuffer, TS_NULL_MLOC, m_urlloc);
      }
      TSMBufferDestroy(m_urlbuffer);
    }
    if (nullptr != m_http_parser) {
      TSHttpParserDestroy(m_http_parser);
    }
  }
};
