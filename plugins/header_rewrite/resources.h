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
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Implement the classes for the various types of hash keys we support.
//
#pragma once

#include <string>

#include "ts/ts.h"
#include "ts/remap.h"

#include "lulu.h"

#if TS_HAS_CRIPTS
#include "cripts/Certs.hpp"
#include "cripts/Transaction.hpp"
#endif

enum ResourceIDs {
  RSRC_NONE                    = 0,
  RSRC_SERVER_RESPONSE_HEADERS = 1 << 0, // 1
  RSRC_SERVER_REQUEST_HEADERS  = 1 << 1, // 2
  RSRC_CLIENT_REQUEST_HEADERS  = 1 << 2, // 4
  RSRC_CLIENT_RESPONSE_HEADERS = 1 << 3, // 8
  RSRC_RESPONSE_STATUS         = 1 << 4, // 16
#if TS_HAS_CRIPTS
  RSRC_CLIENT_CONNECTION  = 1 << 5, // 32
  RSRC_SERVER_CONNECTION  = 1 << 6, // 64
  RSRC_SERVER_CERTIFICATE = 1 << 7, // 128
  RSRC_MTLS_CERTIFICATE   = 1 << 8, // 256
#endif
};

///////////////////////////////////////////////////////////////////////////////
// Resources holds the minimum resources required to process a request.
//
#if !TS_HAS_CRIPTS
struct TransactionState {
  TSHttpTxn txnp = nullptr;
  TSHttpSsn ssnp = nullptr;
};
#endif

class Resources
{
public:
  explicit Resources(TSHttpTxn txnptr, TSCont contptr) : contp(contptr) { _initialize(txnptr, "InkAPI"); }

  explicit Resources(TSHttpTxn txnptr, TSRemapRequestInfo *rri) : _rri(rri) { _initialize(txnptr, "RemapAPI"); }

  ~Resources() { destroy(); }

  // noncopyable
  Resources(const Resources &)      = delete;
  void operator=(const Resources &) = delete;

  void gather(const ResourceIDs ids, TSHttpHookID hook);
  bool
  ready() const
  {
    return _ready;
  }

  TSCont              contp          = nullptr;
  TSRemapRequestInfo *_rri           = nullptr;
  TSMBuffer           bufp           = nullptr;
  TSMLoc              hdr_loc        = nullptr;
  TSMBuffer           client_bufp    = nullptr;
  TSMLoc              client_hdr_loc = nullptr;
#if TS_HAS_CRIPTS
  cripts::Transaction         state; // This now holds txpn / ssnp
  cripts::Client::Connection *client_conn = nullptr;
  cripts::Server::Connection *server_conn = nullptr;
  cripts::Certs::Client      *mtls_cert   = nullptr;
  cripts::Certs::Server      *server_cert = nullptr;
#else
  TransactionState state; // Without cripts, txnp / ssnp goes here
#endif
  const char  *ovector_ptr = nullptr;
  TSHttpStatus resp_status = TS_HTTP_STATUS_NONE;
  int          ovector[OVECCOUNT];
  int          ovector_count = 0;
  bool         changed_url   = false;

private:
  void
  _initialize(TSHttpTxn txnptr, const char *api_type)
  {
    state.txnp = txnptr;
    state.ssnp = TSHttpTxnSsnGet(txnptr); // This is cheap, even if not used

    Dbg(dbg_ctl, "Calling CTOR for Resources (%s)", api_type);
  }

  void destroy();

  bool _ready = false;
};
