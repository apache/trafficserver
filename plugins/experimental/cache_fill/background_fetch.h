/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

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
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include <string>
#include <iostream>
#include <unordered_map>
#include <cinttypes>
#include <string_view>
#include <array>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ts/ts.h"
#include "ts/remap.h"

typedef std::unordered_map<std::string, bool> OutstandingRequests;
const char PLUGIN_NAME[] = "cache_fill";

class BgFetchState
{
public:
  BgFetchState()                     = default;
  BgFetchState(BgFetchState const &) = delete;
  void operator=(BgFetchState const &) = delete;

  static BgFetchState &
  getInstance()
  {
    static BgFetchState _instance;
    return _instance;
  }

  ~BgFetchState() { TSMutexDestroy(_lock); }

  bool
  acquire(const std::string &url)
  {
    bool ret;

    TSMutexLock(_lock);
    if (_urls.end() == _urls.find(url)) {
      _urls[url] = true;
      ret        = true;
    } else {
      ret = false;
    }
    TSMutexUnlock(_lock);

    TSDebug(PLUGIN_NAME, "BgFetchState.acquire(): ret = %d, url = %s", ret, url.c_str());

    return ret;
  }

  bool
  release(const std::string &url)
  {
    bool ret;

    TSMutexLock(_lock);
    if (_urls.end() == _urls.find(url)) {
      ret = false;
    } else {
      _urls.erase(url);
      ret = true;
    }
    TSMutexUnlock(_lock);

    return ret;
  }

private:
  OutstandingRequests _urls;
  TSMutex _lock = TSMutexCreate();
};

//////////////////////////////////////////////////////////////////////////////
// Hold and manage some state for the TXN background fetch continuation.
// This is necessary, because the TXN is likely to not be available
// during the time we fetch from origin.
struct BgFetchData {
  BgFetchData() { memset(&client_ip, 0, sizeof(client_ip)); }

  ~BgFetchData()
  {
    TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdr_loc);
    TSHandleMLocRelease(mbuf, TS_NULL_MLOC, url_loc);

    TSMBufferDestroy(mbuf);

    if (vc) {
      TSError("[%s] Destroyed BgFetchDATA while VC was alive", PLUGIN_NAME);
      TSVConnClose(vc);
      vc = nullptr;
    }

    // If we got schedule, also clean that up
    if (_cont) {
      releaseUrl();

      TSContDestroy(_cont);
      _cont = nullptr;
      TSIOBufferReaderFree(req_io_buf_reader);
      TSIOBufferDestroy(req_io_buf);
      TSIOBufferReaderFree(resp_io_buf_reader);
      TSIOBufferDestroy(resp_io_buf);
    }
  }

  bool
  acquireUrl() const
  {
    return BgFetchState::getInstance().acquire(_url);
  }
  bool
  releaseUrl() const
  {
    return BgFetchState::getInstance().release(_url);
  }

  const char *
  getUrl() const
  {
    return _url.c_str();
  }

  void
  addBytes(int64_t b)
  {
    _bytes += b;
  }

  bool initialize(TSMBuffer request, TSMLoc req_hdr, TSHttpTxn txnp);
  void schedule();

  TSMBuffer mbuf = TSMBufferCreate();
  TSMLoc hdr_loc = TS_NULL_MLOC;
  TSMLoc url_loc = TS_NULL_MLOC;

  struct sockaddr_storage client_ip;

  // This is for the actual background fetch / NetVC
  TSVConn vc                          = nullptr;
  TSIOBuffer req_io_buf               = nullptr;
  TSIOBuffer resp_io_buf              = nullptr;
  TSIOBufferReader req_io_buf_reader  = nullptr;
  TSIOBufferReader resp_io_buf_reader = nullptr;
  TSVIO r_vio                         = nullptr;
  TSVIO w_vio                         = nullptr;

private:
  std::string _url;
  int64_t _bytes = 0;
  TSCont _cont   = nullptr;
};
