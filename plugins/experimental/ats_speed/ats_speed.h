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
#ifndef ATS_SPEED_H_
#define ATS_SPEED_H_

#include <string>

#include <ts/ts.h>

#include "net/instaweb/util/public/google_url.h"
#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"

namespace net_instaweb {

class AtsBaseFetch;
class AtsRewriteOptions;
class AtsServerContext;
class GzipInflater;
class ProxyFetch;
class RewriteOptions;
class RequestHeaders;
class ResponseHeaders;
class ServerContext;

}  // namespace net_instaweb

enum transform_state {
    transform_state_initialized,
    transform_state_output,
    transform_state_finished
};

typedef struct
{
  TSHttpTxn txn;
  TSVIO downstream_vio;
  TSIOBuffer downstream_buffer;
  int64_t downstream_length;
  enum transform_state state;

  net_instaweb::AtsBaseFetch* base_fetch;
  net_instaweb::ProxyFetch* proxy_fetch;
  net_instaweb::GzipInflater* inflater;

  bool write_pending;
  bool fetch_done;
  GoogleString* url_string;
  bool beacon_request;
  bool resource_request;
  bool mps_user_agent;
  bool transform_added;
  net_instaweb::GoogleUrl* gurl;
  net_instaweb::AtsServerContext* server_context;
  GoogleString* user_agent;
  bool html_rewrite;
  const char* request_method;
  int alive;
  net_instaweb::AtsRewriteOptions* options;
  // TODO: Use GoogleString*
  std::string* to_host;
} TransformCtx;

TransformCtx* get_transaction_context(TSHttpTxn txnp);
void ats_ctx_destroy(TransformCtx * ctx);
bool cache_hit(TSHttpTxn txnp);

bool ps_determine_options(net_instaweb::ServerContext* server_context,
  // Directory-specific options, usually null.  They've already been rebased off
  // of the global options as part of the configuration process.
                          net_instaweb::RewriteOptions* directory_options,
                          net_instaweb::RequestHeaders* request_headers,
                          net_instaweb::ResponseHeaders* response_headers,
                          net_instaweb::RewriteOptions** options,
                          net_instaweb::GoogleUrl* url);

void copy_request_headers_to_psol(TSMBuffer bufp, TSMLoc hdr_loc, net_instaweb::RequestHeaders* psol_headers);
// You will own options returned by this:
net_instaweb::AtsRewriteOptions* get_host_options(const StringPiece& host);

#endif /* ATS_SPEED_H_ */
