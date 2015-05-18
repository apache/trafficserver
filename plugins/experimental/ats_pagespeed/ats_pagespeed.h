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

#ifndef ATS_PAGESPEED_H_
#define ATS_PAGESPEED_H_

#include <string>

#include <ts/ts.h>

#include "net/instaweb/util/public/google_url.h"
#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"

#include "net/instaweb/http/public/request_context.h"

namespace net_instaweb
{
class AtsBaseFetch;
class AtsRewriteOptions;
class AtsServerContext;
class GzipInflater;
class InPlaceResourceRecorder;
class ProxyFetch;
class RewriteDriver;
class RewriteOptions;
class RequestHeaders;
class ResponseHeaders;
class ServerContext;

} // namespace net_instaweb

enum transform_state { transform_state_initialized, transform_state_output, transform_state_finished };

typedef struct {
  TSHttpTxn txn;
  TSVIO downstream_vio;
  TSIOBuffer downstream_buffer;
  int64_t downstream_length;
  enum transform_state state;

  net_instaweb::AtsBaseFetch *base_fetch;
  net_instaweb::ProxyFetch *proxy_fetch;
  net_instaweb::GzipInflater *inflater;
  // driver is used for IPRO flow only
  net_instaweb::RewriteDriver *driver;

  bool write_pending;
  bool fetch_done;
  GoogleString *url_string;
  bool beacon_request;
  bool resource_request;
  bool mps_user_agent;
  bool transform_added;
  net_instaweb::GoogleUrl *gurl;
  net_instaweb::AtsServerContext *server_context;
  GoogleString *user_agent;
  bool html_rewrite;
  const char *request_method;
  int alive;
  net_instaweb::AtsRewriteOptions *options;
  // TODO: Use GoogleString*
  std::string *to_host;
  bool in_place;
  bool record_in_place;
  net_instaweb::InPlaceResourceRecorder *recorder;
  net_instaweb::ResponseHeaders *ipro_response_headers;
  bool serve_in_place;
} TransformCtx;

TransformCtx *get_transaction_context(TSHttpTxn txnp);
void ats_ctx_destroy(TransformCtx *ctx);
bool cache_hit(TSHttpTxn txnp);

bool ps_determine_options(net_instaweb::ServerContext *server_context, net_instaweb::RequestHeaders *request_headers,
                          net_instaweb::ResponseHeaders *response_headers, net_instaweb::RewriteOptions **options,
                          net_instaweb::RequestContextPtr request_context, net_instaweb::GoogleUrl *url,
                          GoogleString *pagespeed_query_params, GoogleString *pagespeed_option_cookies, bool html_rewrite);

void copy_request_headers_to_psol(TSMBuffer bufp, TSMLoc hdr_loc, net_instaweb::RequestHeaders *psol_headers);
// You will own options returned by this:
net_instaweb::AtsRewriteOptions *get_host_options(const StringPiece &host, net_instaweb::ServerContext *server_context);

#endif /* ATS_PAGESPEED_H_ */
