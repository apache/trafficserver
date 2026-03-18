/** @file
 *

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

#include "ja4h.h"
#include <openssl/sha2.h>

Extractor::Extractor(TSHttpTxn txnp) : _txn(txnp)
{
  TSHttpTxnClientReqGet(txnp, &(this->_request), &(this->_req_hdr));
}

Extractor::~Extractor()
{
  if (this->_request != nullptr) {
    TSHandleMLocRelease(this->_request, TS_NULL_MLOC, this->_req_hdr);
  }
}

std::string_view
Extractor::get_method()
{
  if (this->_request == nullptr) {
    return "";
  }

  int         method_len;
  const char *method = TSHttpHdrMethodGet(this->_request, this->_req_hdr, &method_len);

  return {method, static_cast<size_t>(method_len)};
}

int
Extractor::get_version()
{
  if (TSHttpTxnClientProtocolStackContains(this->_txn, "h2")) {
    return 2 << 16;
  } else if (TSHttpTxnClientProtocolStackContains(this->_txn, "h2")) {
    return 3 << 16;
  } else {
    return TSHttpHdrVersionGet(this->_request, this->_req_hdr);
  }
}

bool
Extractor::has_cookie_field()
{
  TSMLoc mloc = TSMimeHdrFieldFind(this->_request, this->_req_hdr, TS_MIME_FIELD_COOKIE, TS_MIME_LEN_COOKIE);
  TSHandleMLocRelease(this->_request, this->_req_hdr, mloc);
  return mloc != TS_NULL_MLOC;
}

bool
Extractor::has_referer_field()
{
  TSMLoc mloc = TSMimeHdrFieldFind(this->_request, this->_req_hdr, TS_MIME_FIELD_REFERER, TS_MIME_LEN_REFERER);
  TSHandleMLocRelease(this->_request, this->_req_hdr, mloc);
  return mloc != TS_NULL_MLOC;
}

int
Extractor::get_field_count()
{
  return TSMimeHdrFieldsCount(this->_request, this->_req_hdr);
}

std::string_view
Extractor::get_accept_language()
{
  TSMLoc mloc = TSMimeHdrFieldFind(this->_request, this->_req_hdr, TS_MIME_FIELD_ACCEPT_LANGUAGE, TS_MIME_LEN_ACCEPT_LANGUAGE);
  if (mloc == TS_NULL_MLOC) {
    return {};
  }
  int         value_len;
  const char *value = TSMimeHdrFieldValueStringGet(this->_request, this->_req_hdr, mloc, 0, &value_len);
  TSHandleMLocRelease(this->_request, this->_req_hdr, mloc);
  return {value, static_cast<size_t>(value_len)};
}

void
Extractor::get_headers_hash(unsigned char out[32])
{
  SHA256_CTX sha256ctx;
  SHA256_Init(&sha256ctx);

  TSMLoc field_loc = TSMimeHdrFieldGet(this->_request, this->_req_hdr, 0);

  while (field_loc != TS_NULL_MLOC) {
    int   field_name_len;
    char *field_name = const_cast<char *>(TSMimeHdrFieldNameGet(this->_request, this->_req_hdr, field_loc, &field_name_len));
    if (field_name_len == TS_MIME_LEN_COOKIE) {
      if (std::ranges::equal(std::string_view{field_name, static_cast<size_t>(field_name_len)}, "cookie",
                             [](char c1, char c2) { return c1 == std::tolower(c2); })) {
        continue;
      };
    } else if (field_name_len == TS_MIME_LEN_REFERER) {
      if (std::ranges::equal(std::string_view{field_name, static_cast<size_t>(field_name_len)}, "referer",
                             [](char c1, char c2) { return c1 == std::tolower(c2); })) {
        continue;
      }
    }

    SHA256_Update(&sha256ctx, field_name, field_name_len);

    TSMLoc next_field_loc = TSMimeHdrFieldNext(this->_request, this->_req_hdr, field_loc);
    TSHandleMLocRelease(this->_request, this->_req_hdr, field_loc);
    field_loc = next_field_loc;
  }

  SHA256_Final(out, &sha256ctx);
}
