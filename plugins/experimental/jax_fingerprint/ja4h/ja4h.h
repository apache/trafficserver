#pragma once

#include "ts/ts.h"
#include <string_view>

class Extractor
{
public:
  Extractor(TSHttpTxn txnp);
  ~Extractor();
  std::string_view get_method();
  int              get_version();
  bool             has_cookie_field();
  bool             has_referer_field();
  int              get_field_count();
  std::string_view get_accept_language();
  void             get_headers_hash(unsigned char out[32]);

private:
  TSHttpTxn _txn;
  TSMBuffer _request = nullptr;
  TSMLoc    _req_hdr = nullptr;
  ;
};
