/** @file

  A brief file description

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

#include <string>
#include <list>
#include <vector>
#include <netinet/in.h>

#include "ts/ts.h"
#include "lib/StringHash.h"
#include "lib/HttpHeader.h"
#include "HttpDataFetcher.h"

class HttpDataFetcherImpl : public HttpDataFetcher
{
public:
  HttpDataFetcherImpl(TSCont contp, sockaddr const *client_addr, const char *debug_tag);

  void useHeader(const EsiLib::HttpHeader &header);

  void useHeaders(const EsiLib::HttpHeaderList &headers);

  bool addFetchRequest(const std::string &url, FetchedDataProcessor *callback_obj = nullptr) override;

  bool handleFetchEvent(TSEvent event, void *edata);

  bool
  isFetchEvent(TSEvent event) const
  {
    int base_event_id;
    return _isFetchEvent(event, base_event_id);
  }

  bool
  isFetchComplete() const
  {
    return (_n_pending_requests == 0);
  };

  DataStatus getRequestStatus(const std::string &url) const override;

  int
  getNumPendingRequests() const override
  {
    return _n_pending_requests;
  };

  // used to return data to callers
  struct ResponseData {
    const char *content;
    int content_len;
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    TSHttpStatus status;
    ResponseData() { set(nullptr, 0, nullptr, nullptr, TS_HTTP_STATUS_NONE); }
    inline void set(const char *c, int clen, TSMBuffer b, TSMLoc loc, TSHttpStatus s);
    void
    clear()
    {
      set(nullptr, 0, nullptr, nullptr, TS_HTTP_STATUS_NONE);
    }
  };

  bool getData(const std::string &url, ResponseData &resp_data) const;

  bool
  getContent(const std::string &url, const char *&content, int &content_len) const override
  {
    ResponseData resp;
    if (getData(url, resp)) {
      content     = resp.content;
      content_len = resp.content_len;
      return true;
    }
    return false;
  }

  void clear();

  ~HttpDataFetcherImpl() override;

private:
  TSCont _contp;
  char _debug_tag[64];

  typedef std::list<FetchedDataProcessor *> CallbackObjectList;

  // used to track a request that was made
  struct RequestData {
    std::string response;
    std::string raw_response;
    const char *body         = nullptr;
    int body_len             = 0;
    TSHttpStatus resp_status = TS_HTTP_STATUS_NONE;
    CallbackObjectList callback_objects;
    bool complete  = false;
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    RequestData() {}
  };

  typedef __gnu_cxx::hash_map<std::string, RequestData, EsiLib::StringHasher> UrlToContentMap;
  UrlToContentMap _pages;

  typedef std::vector<UrlToContentMap::iterator> IteratorArray;
  IteratorArray _page_entry_lookup; // used to map event ids to requests

  int _n_pending_requests;
  int _curr_event_id_base;
  TSHttpParser _http_parser;

  static const int FETCH_EVENT_ID_BASE;

  int
  _getBaseEventId(TSEvent event) const
  {
    return (static_cast<int>(event) - FETCH_EVENT_ID_BASE) / 3; // integer division
  }

  bool _isFetchEvent(TSEvent event, int &base_event_id) const;
  bool _checkHeaderValue(TSMBuffer bufp, TSMLoc hdr_loc, const char *name, int name_len, const char *exp_value, int exp_value_len,
                         bool prefix) const;

  std::string _headers_str;

  inline void _release(RequestData &req_data);

  struct sockaddr_storage _client_addr;
};

inline void
HttpDataFetcherImpl::ResponseData::set(const char *c, int clen, TSMBuffer b, TSMLoc loc, TSHttpStatus s)
{
  content     = c;
  content_len = clen;
  bufp        = b;
  hdr_loc     = loc;
  status      = s;
}
