// Copyright 2013 We-Amp B.V.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: oschaaf@we-amp.com (Otto van der Schaaf)
#ifndef ATS_BASE_FETCH_H_
#define ATS_BASE_FETCH_H_

#include <string>

#include <ts/ts.h>

#include "ats_speed.h"

#include "net/instaweb/http/public/async_fetch.h"
#include "net/instaweb/http/public/headers.h"
#include "net/instaweb/util/public/string.h"


namespace net_instaweb {

class AtsServerContext;
class AbstractMutex;

class AtsBaseFetch : public net_instaweb::AsyncFetch {

public:
  // TODO(oschaaf): change this to take the downstream buffer and vio
  // instead of AtsData*. Also, make the bytes send a property
  // of the fetch itself instead of tracking it on data.
  // Doing so, would allow us to share this with the server intercept
  // code for resources.
  AtsBaseFetch(AtsServerContext* server_context,
               const net_instaweb::RequestContextPtr& request_ctx,
               TSVIO downstream_vio,
               TSIOBuffer downstream_buffer,
               bool is_resource_fetch);
  
  virtual ~AtsBaseFetch();
  void Release();
private:
  virtual bool HandleWrite(const StringPiece& sp, net_instaweb::MessageHandler* handler);
  virtual bool HandleFlush( net_instaweb::MessageHandler* handler);
  virtual void HandleHeadersComplete();
  virtual void HandleDone(bool success);
  void Lock();
  void Unlock();
  void DecrefAndDeleteIfUnreferenced();
  void ForwardData(const StringPiece& sp, bool reenable, bool last);
  GoogleString buffer_;
  AtsServerContext* server_context_;
  bool done_called_;
  bool last_buf_sent_;

  // How many active references there are to this fetch. Starts at two,
  // decremented once when Done() is called and once when Release() is called.
  int references_;
  TSVIO downstream_vio_;
  TSIOBuffer downstream_buffer_;
  bool is_resource_fetch_;
  int64_t downstream_length_;

  // We don't own this mutex
  TSMutex txn_mutex_;
};

} /* ats_pagespeed */


#endif /* ATS_BASE_FETCH_H_ */
