/** @file

  Implementation of HTTP utilities for ESI plugins.

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

#include "http_utils.h"

#include <ts/ts.h>

std::string
getRequestUrlString(TSHttpTxn txnp)
{
  std::string request_url = UNKNOWN_URL_STRING;
  TSMBuffer   req_bufp;
  TSMLoc      req_hdr_loc;
  TSMLoc      url_loc;

  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_hdr_loc) == TS_SUCCESS) {
    if (TSHttpTxnPristineUrlGet(txnp, &req_bufp, &url_loc) == TS_SUCCESS) {
      request_url = getUrlString(req_bufp, url_loc);
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, url_loc);
    }
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr_loc);
  }

  return request_url;
}

std::string
getUrlString(TSMBuffer bufp, TSMLoc url_loc)
{
  std::string url_string;
  int         url_len = 0;
  char       *url_ptr = TSUrlStringGet(bufp, url_loc, &url_len);

  if (url_ptr) {
    url_string.assign(url_ptr, url_len);
    TSfree(url_ptr);
  } else {
    url_string = UNKNOWN_URL_STRING;
  }

  return url_string;
}
