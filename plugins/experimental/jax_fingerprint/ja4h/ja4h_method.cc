/** @file

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

#include "ts/ts.h"

#include "../plugin.h"
#include "../context.h"
#include "ja4h_method.h"
#include "ja4h.h"
#include "datasource.h"

#include "openssl/sha.h"

namespace ja4h_method
{
void on_request(JAxContext *, TSHttpTxn);

struct Method method = {
  "JA4H",
  Method::Type::REQUEST_BASED,
  nullptr,
  on_request,
};

class TxnDatasource : public Datasource
{
public:
  TxnDatasource(TSHttpTxn txnp);
  ~TxnDatasource();
  std::string_view get_method() override;
  int              get_version() override;
  bool             has_cookie_field() override;
  bool             has_referer_field() override;
  int              get_field_count() override;
  std::string_view get_accept_language() override;
  void             get_headers_hash(unsigned char out[32]) override;

private:
  TSHttpTxn _txn;
  TSMBuffer _request = nullptr;
  TSMLoc    _req_hdr = nullptr;
};

TxnDatasource::TxnDatasource(TSHttpTxn txnp) : _txn(txnp)
{
  TSHttpTxnClientReqGet(txnp, &(this->_request), &(this->_req_hdr));
}

TxnDatasource::~TxnDatasource()
{
  if (this->_request != nullptr) {
    TSHandleMLocRelease(this->_request, TS_NULL_MLOC, this->_req_hdr);
  }
}

std::string_view
TxnDatasource::get_method()
{
  if (this->_request == nullptr) {
    return "";
  }

  int         method_len;
  const char *method = TSHttpHdrMethodGet(this->_request, this->_req_hdr, &method_len);

  return {method, static_cast<size_t>(method_len)};
}

int
TxnDatasource::get_version()
{
  if (TSHttpTxnClientProtocolStackContains(this->_txn, "h2")) {
    return 2 << 16;
  } else if (TSHttpTxnClientProtocolStackContains(this->_txn, "h3")) {
    return 3 << 16;
  } else {
    return TSHttpHdrVersionGet(this->_request, this->_req_hdr);
  }
}

bool
TxnDatasource::has_cookie_field()
{
  TSMLoc mloc = TSMimeHdrFieldFind(this->_request, this->_req_hdr, TS_MIME_FIELD_COOKIE, TS_MIME_LEN_COOKIE);
  if (mloc) {
    TSHandleMLocRelease(this->_request, this->_req_hdr, mloc);
  }
  return mloc != TS_NULL_MLOC;
}

bool
TxnDatasource::has_referer_field()
{
  TSMLoc mloc = TSMimeHdrFieldFind(this->_request, this->_req_hdr, TS_MIME_FIELD_REFERER, TS_MIME_LEN_REFERER);
  if (mloc) {
    TSHandleMLocRelease(this->_request, this->_req_hdr, mloc);
  }
  return mloc != TS_NULL_MLOC;
}

int
TxnDatasource::get_field_count()
{
  return TSMimeHdrFieldsCount(this->_request, this->_req_hdr);
}

std::string_view
TxnDatasource::get_accept_language()
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
TxnDatasource::get_headers_hash(unsigned char out[32])
{
  SHA256_CTX sha256ctx;
  SHA256_Init(&sha256ctx);

  TSMLoc field_loc = TSMimeHdrFieldGet(this->_request, this->_req_hdr, 0);

  while (field_loc != TS_NULL_MLOC) {
    int   field_name_len;
    char *field_name = const_cast<char *>(TSMimeHdrFieldNameGet(this->_request, this->_req_hdr, field_loc, &field_name_len));

    if (this->_should_include_field({field_name, static_cast<size_t>(field_name_len)})) {
      SHA256_Update(&sha256ctx, field_name, field_name_len);
    }

    TSMLoc next_field_loc = TSMimeHdrFieldNext(this->_request, this->_req_hdr, field_loc);
    TSHandleMLocRelease(this->_request, this->_req_hdr, field_loc);
    field_loc = next_field_loc;
  }

  SHA256_Final(out, &sha256ctx);
}

} // namespace ja4h_method

void
ja4h_method::on_request(JAxContext *ctx, TSHttpTxn txnp)
{
  char          fingerprint[FINGERPRINT_LENGTH];
  TxnDatasource datasource{txnp};

  generate_ja4h_fingerprint(fingerprint, datasource);

  ctx->set_fingerprint({fingerprint, JA4H_FINGERPRINT_LENGTH});
}
