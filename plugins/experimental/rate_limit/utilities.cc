/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tscore/ink_config.h"
#include "ts/ts.h"
#include "ts/remap.h"
#include "utilities.h"

///////////////////////////////////////////////////////////////////////////////
// Add a header with the delay imposed on this transaction. This can be used
// for logging, and other types of metrics.
//
void
delayHeader(TSHttpTxn txnp, std::string &header, std::chrono::milliseconds delay)
{
  if (header.size() > 0) {
    TSMLoc hdr_loc   = nullptr;
    TSMBuffer bufp   = nullptr;
    TSMLoc field_loc = nullptr;

    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, header.c_str(), header.size(), &field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueIntSet(bufp, hdr_loc, field_loc, -1, static_cast<int>(delay.count()))) {
          TSDebug(PLUGIN_NAME, "Added client request header; %s: %d", header.c_str(), static_cast<int>(delay.count()));
          TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        }
        TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Add a header with the delay imposed on this transaction. This can be used
// for logging, and other types of metrics.
//
void
retryAfter(TSHttpTxn txnp, unsigned retry)
{
  if (retry > 0) {
    TSMLoc hdr_loc   = nullptr;
    TSMBuffer bufp   = nullptr;
    TSMLoc field_loc = nullptr;

    if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, "Retry-After", 11, &field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueIntSet(bufp, hdr_loc, field_loc, -1, retry)) {
          TSDebug(PLUGIN_NAME, "Added a Retry-After: %u", retry);
          TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        }
        TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Parse a URL to obtain a description for use with metrics when no user
// provided tag is available. This is used by the remap side of the plugin,
// while the SNI side uses the FQDN associated with each limiter instance
// which is obtained from the list of SNIs in the global plugin configuration.
//
std::string
getDescriptionFromUrl(const char *url)
{
  TSMBuffer const buf = TSMBufferCreate();
  TSMLoc url_loc      = nullptr;

  const int url_len = strlen(url);
  std::string description;

  if (TS_SUCCESS == TSUrlCreate(buf, &url_loc) && TS_PARSE_DONE == TSUrlParse(buf, url_loc, &url, url + url_len)) {
    int host_len, scheme_len = 0;
    const char *s  = TSUrlSchemeGet(buf, url_loc, &scheme_len);
    const char *h  = TSUrlHostGet(buf, url_loc, &host_len);
    const int port = TSUrlPortGet(buf, url_loc);

    const std::string hostname = std::string(h, host_len);
    const std::string scheme   = std::string(s, scheme_len);

    TSDebug(PLUGIN_NAME, "scheme = %s, host = %s, port = %d", scheme.c_str(), hostname.c_str(), port);

    description = scheme;
    description.append(".");
    description.append(hostname);

    // only append the port when it is non-standard
    if (!(strncmp(s, TS_URL_SCHEME_HTTP, scheme_len) == 0 && port == 80) &&
        !(strncmp(s, TS_URL_SCHEME_HTTPS, scheme_len) == 0 && port == 443)) {
      description.append(":" + std::to_string(port));
    }
  }

  if (url_loc != nullptr) {
    TSHandleMLocRelease(buf, nullptr, url_loc);
  }

  TSMBufferDestroy(buf);

  return description;
}
